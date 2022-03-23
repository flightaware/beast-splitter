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

#ifndef STATUS_WRITER_H
#define STATUS_WRITER_H

#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>

#include "beast_input.h"
#include "modes_filter.h"
#include "modes_message.h"

namespace splitter {
    class StatusWriter : public std::enable_shared_from_this<StatusWriter> {
      public:
        typedef std::shared_ptr<StatusWriter> pointer;

        const std::chrono::milliseconds timeout_interval = std::chrono::milliseconds(2500);

        // factory method, this class must always be constructed via make_shared
        static pointer create(boost::asio::io_service &service, modes::FilterDistributor &distributor, beast::BeastInput::pointer input, const std::string &path) { return pointer(new StatusWriter(service, distributor, input, path)); }

        void start();
        void close();

      private:
        StatusWriter(boost::asio::io_service &service_, modes::FilterDistributor &distributor_, beast::BeastInput::pointer input_, const std::string &path);

        void write(const modes::Message &message);
        void reset_timeout();
        void status_timeout(const boost::system::error_code &ec = boost::system::error_code());
        void write_status_file(const std::string &gps_color = std::string(), const std::string &gps_message = std::string(), int pps_offset = -9999);

        boost::asio::io_service &service;
        modes::FilterDistributor &distributor;
        beast::BeastInput::pointer input;
        std::string path;

        std::string temppath;
        modes::FilterDistributor::handle filter_handle;
        boost::asio::steady_timer timeout_timer;
    };
}; // namespace splitter

#endif
