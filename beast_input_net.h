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

#ifndef BEAST_INPUT_NET_H
#define BEAST_INPUT_NET_H

#include <boost/asio/ip/tcp.hpp>

#include "beast_input.h"

namespace beast {
    class NetInput : public BeastInput {
    public:
        typedef std::shared_ptr<NetInput> pointer;

        // the number of bytes to try to read at a time from the connection
        const size_t read_buffer_size = 4096;

        // factory method
        static pointer create(boost::asio::io_service &service,
                              const std::string &host,
                              const std::string &port_or_service,
                              const Settings &fixed_settings = Settings(),
                              const modes::Filter &filter = modes::Filter())
        {
            return pointer(new NetInput(service,
                                        host, port_or_service,
                                        fixed_settings,
                                        filter));
        }

    protected:
        std::string what() const override;
        void try_to_connect(void) override;
        void disconnect(void) override;
        bool low_level_write(std::shared_ptr<helpers::bytebuf> message) override;

    private:
        // construct a new net input instance, don't start yet
        NetInput(boost::asio::io_service &service_,
                 const std::string &host_,
                 const std::string &port_or_service_,
                 const Settings &fixed_settings_,
                 const modes::Filter &filter_);

        void resolve_and_connect(const boost::system::error_code &ec = boost::system::error_code());
        void try_next_endpoint();
        void connection_established(const boost::asio::ip::tcp::endpoint &endpoint);
        void start_reading(const boost::system::error_code &ec = boost::system::error_code());
        void handle_error(const boost::system::error_code &ec);
        void check_framing_errors(void);

        std::string host;
        std::string port_or_service;

        boost::asio::ip::tcp::resolver resolver;
        boost::asio::ip::tcp::socket socket;
        boost::asio::steady_timer reconnect_timer;
        boost::asio::ip::tcp::resolver::iterator next_endpoint;

        // cached buffer used for reads
        std::shared_ptr<helpers::bytebuf> readbuf;

        // have we warned about a possibly bad protocol?
        bool warned_about_framing;
    };
};

#endif
