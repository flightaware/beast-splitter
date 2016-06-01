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

#include <algorithm>
#include <iomanip>
#include <boost/asio.hpp>

#include "beast_input_serial.h"
#include "modes_message.h"

using namespace beast;

SerialInput::SerialInput(boost::asio::io_service &service_,
                         const std::string &path_,
                         unsigned int fixed_baud_rate_,
                         const Settings &fixed_settings_,
                         const modes::Filter &filter_)
    : BeastInput(service_, fixed_settings_, filter_),
      path(path_),
      port(service_),
      autobaud_interval(autobaud_base_interval),
      autobaud_timer(service_),
      read_timer(service_),
      readbuf(std::make_shared<helpers::bytebuf>(read_buffer_size)),
      warned_about_rate(false)
{
    // set up autobaud
    if (fixed_baud_rate_ == 0) {
        autobauding = true;
        autobaud_rates.assign(autobaud_standard_rates.begin(), autobaud_standard_rates.end());
        autobaud_rate = autobaud_rates.begin();
        baud_rate = *autobaud_rate;
    } else {
        autobauding = false;
        baud_rate = fixed_baud_rate_;
    }
}

std::string SerialInput::what() const
{
    return std::string("serial(") + path + std::string(")");
}

void SerialInput::try_to_connect(void)
{
    auto self(shared_from_this());

    std::cerr << what() << ": opening port at " << baud_rate << "bps" << std::endl;

    try {
        if (port.is_open())
            port.cancel();
        else
            port.open(path);
        port.set_option(boost::asio::serial_port_base::character_size(8));
        port.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        port.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        port.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::hardware));
        port.set_option(boost::asio::serial_port_base::baud_rate(baud_rate));
    } catch (const boost::system::system_error &err) {
        handle_error(err.code());
        return;
    }

    if (autobaud_rates.size() > 1) {
        autobaud_timer.expires_from_now(autobaud_interval);

        autobaud_timer.async_wait([this,self] (const boost::system::error_code &ec) {
                if (!ec) {
                    advance_autobaud();
                }
            });
    }

    connection_established();
    start_reading();
}

void SerialInput::disconnect()
{
    autobaud_timer.cancel();
    read_timer.cancel();
    if (port.is_open()) {
        boost::system::error_code ignored;
        port.close(ignored);
    }
}

void SerialInput::low_level_write(std::shared_ptr<helpers::bytebuf> message)
{
    if (!port.is_open())
        return;

    auto self(shared_from_this());
    boost::asio::async_write(port, boost::asio::buffer(*message),
                             [this,self,message] (boost::system::error_code ec, std::size_t len) {
                                 if (ec)
                                     handle_error(ec);
                             });
}

void SerialInput::handle_error(const boost::system::error_code &ec)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    std::cerr << what() << ": i/o error: " << ec.message() << std::endl;
    connection_failed();

    autobaud_timer.cancel();
    read_timer.cancel();
    if (port.is_open()) {
        boost::system::error_code ignored;
        port.close(ignored);
    }

    // reset autobaud state
    if (!autobaud_rates.empty()) {
        autobauding = true;
        autobaud_interval = autobaud_base_interval;
        autobaud_rate = autobaud_rates.begin();
        baud_rate = *autobaud_rate;
    }
}

void SerialInput::advance_autobaud(void)
{
    if (!autobauding)
        return;

    if (++autobaud_rate == autobaud_rates.end()) {
        // Ran out of rates to try. Increase the interval and start again
        std::cerr << what() << ": autobaud failed, trying again (consider specifying --fixed-baud)" << std::endl;
        autobaud_interval = std::min(autobaud_max_interval, autobaud_interval * 2);
        autobaud_rate = autobaud_rates.begin();
    }

    baud_rate = *autobaud_rate;
    try_to_connect();
}

void SerialInput::check_framing_errors(void)
{
    if (!autobauding && !have_good_sync() && bad_bytes() > autobaud_restart_bytes) {
        if (!autobaud_rates.empty()) {
            // We did autobaud and picked a rate, but it's not really working, let's
            // restart autobauding.
            std::cerr << what() << ": too many framing errors seen, restarting autobauding" << std::endl;
            autobauding = true;
            advance_autobaud();
        } else {
            if (!warned_about_rate) {
                std::cerr << what() << ": many framing errors seen, is the baud rate (" << baud_rate << " bps) correct?" << std::endl;
                warned_about_rate = true;
            }
        }
    }
}

void SerialInput::start_reading(const boost::system::error_code &ec)
{
    if (ec) {
        assert (ec == boost::asio::error::operation_aborted);
        return;
    }

    auto self(std::static_pointer_cast<SerialInput>(shared_from_this()));
    std::shared_ptr<helpers::bytebuf> buf;

    buf.swap(readbuf);
    if (buf) {
        buf->resize(read_buffer_size);
    } else {
        buf = std::make_shared<helpers::bytebuf>(read_buffer_size);
    }

    read_timer.expires_from_now(read_interval);
    port.async_read_some(boost::asio::buffer(*buf),
                         [this,self,buf] (const boost::system::error_code &ec, std::size_t len) {
                             if (ec) {
                                 readbuf = buf;
                                 handle_error(ec);
                             } else {
                                 buf->resize(len);
                                 parse_input(*buf);
                                 check_framing_errors();
                                 readbuf = buf;

                                 // If we didn't get a full-ish buffer, then wait a bit before the next read so we don't
                                 // spin reading only a few bytes each time.

                                 // (unfortunately, boost::asio's edge-triggered epoll still gets woken repeatedly each time a
                                 // little more data arrives, but at least we don't have to do a bunch of work on every one of
                                 // those)
                                 if (len < read_buffer_size*3/4) {
                                     read_timer.async_wait(std::bind(&SerialInput::start_reading, self, std::placeholders::_1));
                                 } else {
                                     start_reading();
                                 }
                             }
                         });
}

void SerialInput::saw_good_message()
{
    BeastInput::saw_good_message();

    if (autobauding && good_messages() >= autobaud_good_messages) {
        // Accept this rate
        std::cerr << what() << ": autobaud selected " << baud_rate << " bps" << std::endl;
        autobauding = false;
        autobaud_timer.cancel();
    }
}

bool SerialInput::can_dispatch() const
{
    return (!autobauding && BeastInput::can_dispatch());
}

