// -*- c++ -*-

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

#ifndef BEAST_INPUT_SERIAL_H
#define BEAST_INPUT_SERIAL_H

#include <boost/asio/serial_port.hpp>

#include "beast_input.h"

namespace beast {
    class SerialInput : public BeastInput {
    public:
        typedef std::shared_ptr<SerialInput> pointer;

        // the standard baud rates to try, in their preferred order
        const std::array<unsigned int,2> autobaud_standard_rates { { 3000000, 1000000 } };

        // the initial interval to wait for sufficient good messages (see autobaud_good_messages) before changing baud rates
        const std::chrono::milliseconds autobaud_base_interval = std::chrono::milliseconds(1000);

        // the maximum interval between changing baud rates
        const std::chrono::milliseconds autobaud_max_interval = std::chrono::milliseconds(16000);

        // the number of consecutive messages without sync errors needed before the baud rate is fixed
        const unsigned int autobaud_good_messages = 4;

        // the number of bytes without good sync before restarting autobauding
        const unsigned int autobaud_restart_bytes = 1000;

        // the number of bytes to try to read at a time from the connection
        const size_t read_buffer_size = 4096;

        // how long to wait between scheduling reads (to reduce the spinning on short messages)
        const std::chrono::milliseconds read_interval = std::chrono::milliseconds(50);

        // factory method
        static pointer create(boost::asio::io_service &service,
                              const std::string &path,
                              unsigned int fixed_baud_rate = 0,
                              const Settings &fixed_settings = Settings(),
                              const modes::Filter &filter = modes::Filter())
        {
            return pointer(new SerialInput(service, path,
                                           fixed_baud_rate,
                                           fixed_settings,
                                           filter));
        }

    protected:
        std::string what() const override;
        void try_to_connect(void) override;
        void disconnect(void) override;
        void low_level_write(std::shared_ptr<helpers::bytebuf> message) override;
        void saw_good_message(void) override;
        bool can_dispatch(void) const override;

    private:
        // construct a new serial input instance, don't start yet
        SerialInput(boost::asio::io_service &service_,
                    const std::string &path_,
                    unsigned int fixed_baud_rate,
                    const Settings &fixed_settings_,
                    const modes::Filter &filter_);

        void start_reading(const boost::system::error_code &ec = boost::system::error_code());
        void advance_autobaud(void);
        void handle_error(const boost::system::error_code &ec);
        void check_framing_errors(void);

        // path to the serial device
        std::string path;

        // the port we're using
        boost::asio::serial_port port;

        // true if we are actively hunting for the correct baud rate
        bool autobauding;

        // vector of baud rates to try, single entry if a fixed rate is set
        std::vector<unsigned int> autobaud_rates;

        // current iterator into autobaud_rates
        std::vector<unsigned int>::iterator autobaud_rate;

        // actual rate in use
        unsigned int baud_rate;

        // how long to wait between autobaud attempts; doubles (up to a limit)
        // each time all rates in autobaud_rates have been tried
        std::chrono::milliseconds autobaud_interval;

        // timer that expires after autobaud_interval
        boost::asio::steady_timer autobaud_timer;

        // timer that expires when we want to read some more data
        boost::asio::steady_timer read_timer;

        // cached buffer used for reads
        std::shared_ptr<helpers::bytebuf> readbuf;

        // have we warned about a possibly bad baud rate?
        bool warned_about_rate;
    };
};

#endif
