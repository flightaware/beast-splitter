// Copyright (c) 2015-2016, FlightAware LLC.
// Copyright (c) 2015, Oliver Jowett <oliver@mutability.co.uk>
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.

// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <iostream>
#include <fstream>
#include <sstream>

#include "status_writer.h"
#include "modes_message.h"

namespace asio = boost::asio;

namespace splitter {
    StatusWriter::StatusWriter(asio::io_service &service_,
                               modes::FilterDistributor &distributor_,
                               beast::BeastInput::pointer input_,
                               const std::string &path_)
        : service(service_),
          distributor(distributor_),
          input(input_),
          path(path_),
          timeout_timer(service_)
    {
        temppath = path_ + ".new";
    }

    void StatusWriter::start()
    {
        auto self(shared_from_this());

        modes::Filter filter;
        filter.receive_status = true;
        filter_handle = distributor.add_client(std::bind(&StatusWriter::write, self, std::placeholders::_1), filter);

        reset_timeout();
    }

    void StatusWriter::close()
    {
        timeout_timer.cancel();
        distributor.remove_client(filter_handle);
    }

    void StatusWriter::reset_timeout()
    {
        auto self(shared_from_this());

        timeout_timer.expires_from_now(timeout_interval); // will cancel previous async_wait
        timeout_timer.async_wait(std::bind(&StatusWriter::status_timeout, self, std::placeholders::_1));
    }

    void StatusWriter::status_timeout(const boost::system::error_code &ec)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        reset_timeout();
        if (input && input->is_connected() && input->receiver() == beast::ReceiverType::RADARCAPE) {
            // we should be getting status messages, but we are not.
            write_status_file("red", "No recent GPS status message received");
        } else {
            // Not connected or it's a regular Beast, no GPS status.
            write_status_file();
        }
    }

    void StatusWriter::write(const modes::Message &message)
    {
        if (message.type() != modes::MessageType::STATUS)
            return;

        reset_timeout();

        const auto &data = message.data();

        // 0: settings, including:
        //    10: 1=GPS timestamps, 0=12MHz timestamps
        // 1: signed timestamp offset at last PPS edge, 15ns units
        // 2: GPS status
        //    80: 1=UTC, 0=UTC+1; if 0, other bits are unused
        //    40: unused
        //    20: 1=timestamp from FPGA, 0=timestamp from GPS
        //    10: 1=degradation <= 45ms, 0=degradation > 45ms
        //    08: 1=UTC time, 0=GPS time
        //    04: 1=good sats, 0=not enough good sats
        //    02: 1=tracking sats, 0=no sats
        //    01: 1=antenna OK, 0=antenna fault

        if (!(data[0] & 0x10)) {
            // 12MHz mode
            write_status_file("red", "Not in GPS timestamp mode");
            return;
        }

        if (!(data[2] & 0x80)) {
            // Old style message. Assume it's good if abs(degradation) < 45ms
            if (data[1] <= 3 || data[1] >= (256-3)) {
                write_status_file("green", "Receiver synchronized to GPS time");
            } else {
                write_status_file("amber", "Receiver more than 45ns from GPS time");
            }
            return;
        }

        // New style message

        if (!(data[2] & 0x20)) {
            // FPGA is using GPS time
            if (data[2] & 0x10) {
                write_status_file("green", "Receiver synchronized to GPS time");
            } else {
                write_status_file("amber", "Receiver more than 45ns from GPS time");
            }
            return;
        }

        // FPGA is not using GPS time, work out why.

        std::vector<std::string> status_messages;
        if (!(data[2] & 0x08)) {
            status_messages.push_back("GPS/UTC time offset not known");
        }

        if (!(data[2] & 0x02)) {
            status_messages.push_back("Not tracking any satellites");
        } else if (!(data[2] & 0x04)) {
            status_messages.push_back("Not tracking sufficient satellites");
        }

        if (!(data[2] & 0x01)) {
            status_messages.push_back("Antenna fault");
        }

        if (status_messages.empty()) {
            status_messages.push_back("Unrecognized GPS fault");
        }

        std::ostringstream status_buffer;
        for (auto i = status_messages.begin(); i != status_messages.end(); ++i) {
            if (i != status_messages.begin())
                status_buffer << "; ";
            status_buffer << *i;
        }

        write_status_file("red", status_buffer.str());
    }

    void StatusWriter::write_status_file(const std::string &gps_color, const std::string &gps_message)
    {
        // This is simple enough we don't bother with a JSON library.
        // NB: we assume that the status messages do not need escaping.

        auto unix_epoch = std::chrono::system_clock::from_time_t(0);
        auto now = std::chrono::system_clock::now();
        auto expiry = now + timeout_interval * 2;

        std::ofstream outf(temppath, std::ios::out | std::ios::trunc);
        outf << "{" << std::endl;

        if (input) {
            std::string radio_color = (input->is_connected() ? "green" : "red");
            std::string radio_message = (input->is_connected() ? "Connected to receiver" : "Not connected to receiver");

            outf << "  \"radio\"    : {" << std::endl
                 << "    \"status\"  : \"" << (input->is_connected() ? "green" : "red") << "\"," << std::endl
                 << "    \"message\" : \"" << (input->is_connected() ? "Connected to receiver" : "Not connected to receiver") << "\"" << std::endl
                 << "  }," << std::endl;
        }

        if (!gps_color.empty()) {
            outf << "  \"gps\"      : {" << std::endl
                 << "    \"status\"  : \"" << gps_color << "\"," << std::endl
                 << "    \"message\" : \"" << gps_message << "\"" << std::endl
                 << "  }," << std::endl;
        }

        outf << "  \"time\"     : " << std::chrono::duration_cast<std::chrono::milliseconds>(now - unix_epoch).count() << "," << std::endl
             << "  \"expiry\"   : " << std::chrono::duration_cast<std::chrono::milliseconds>(expiry - unix_epoch).count() << "," << std::endl
             << "  \"interval\" : " << std::chrono::duration_cast<std::chrono::milliseconds>(timeout_interval).count() << std::endl
             << "}" << std::endl;
        outf.close();

        if (outf) {
            std::rename(temppath.c_str(), path.c_str());
        }
    }
}
