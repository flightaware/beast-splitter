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
#include <iostream>

#include <boost/asio.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <boost/asio/steady_timer.hpp>

#include "beast_input_net.h"
#include "modes_message.h"

using namespace beast;
using boost::asio::ip::tcp;

NetInput::NetInput(boost::asio::io_service &service_, const std::string &host_, const std::string &port_or_service_, const Settings &fixed_settings_, const modes::Filter &filter_) : BeastInput(service_, fixed_settings_, filter_), host(host_), port_or_service(port_or_service_), resolver(service_), socket(service_), reconnect_timer(service_), readbuf(std::make_shared<helpers::bytebuf>(read_buffer_size)), warned_about_framing(false) {}

std::string NetInput::what() const { return std::string("net(") + host + std::string(":") + port_or_service + std::string(")"); }

void NetInput::try_to_connect(void) {
    auto self(shared_from_this());

    tcp::resolver::query query(host, port_or_service);
    resolver.async_resolve(query, [this, self](const boost::system::error_code &ec, tcp::resolver::iterator it) {
        if (!ec) {
            next_endpoint = it;
            try_next_endpoint();
        } else if (ec == boost::asio::error::operation_aborted) {
            return;
        } else {
            std::cerr << what() << ": could not resolve address: " << ec.message() << std::endl;
            connection_failed();
            return;
        }
    });
}

void NetInput::try_next_endpoint() {
    if (next_endpoint == tcp::resolver::iterator()) {
        // No more addresses to try
        connection_failed();
        return;
    }

    tcp::endpoint endpoint = *next_endpoint++;

    auto self(shared_from_this());
    socket.async_connect(endpoint, [this, self, endpoint](const boost::system::error_code &ec) {
        if (!ec) {
            connection_established(endpoint);
        } else if (ec == boost::asio::error::operation_aborted) {
            return;
        } else {
            std::cerr << what() << ": connection to " << endpoint << " failed: " << ec.message() << std::endl;
            socket.close();
            try_next_endpoint();
        }
    });
}

void NetInput::connection_established(const tcp::endpoint &endpoint) {
    std::cerr << what() << ": connected to " << endpoint << std::endl;

    BeastInput::connection_established();
    warned_about_framing = false;
    start_reading();
}

void NetInput::disconnect() {
    if (socket.is_open()) {
        boost::system::error_code ignored;
        socket.close(ignored);
    }
}

bool NetInput::low_level_write(std::shared_ptr<helpers::bytebuf> message) {
    if (!socket.is_open())
        return false;

    auto self(shared_from_this());
    boost::asio::async_write(socket, boost::asio::buffer(*message), [this, self, message](boost::system::error_code ec, std::size_t len) {
        if (ec)
            handle_error(ec);
    });
    return true;
}

void NetInput::handle_error(const boost::system::error_code &ec) {
    if (ec == boost::asio::error::operation_aborted)
        return;

    std::cerr << what() << ": i/o error: " << ec.message() << std::endl;
    connection_failed();

    if (socket.is_open()) {
        boost::system::error_code ignored;
        socket.close(ignored);
    }
}

void NetInput::check_framing_errors(void) {
    if (!have_good_sync() && bad_bytes() > 20) {
        if (!warned_about_framing) {
            std::cerr << what() << ": framing errors seen, is the peer sending Beast binary data?" << std::endl;
            warned_about_framing = true;
        }
    }
}

void NetInput::start_reading(const boost::system::error_code &ec) {
    if (ec) {
        assert(ec == boost::asio::error::operation_aborted);
        return;
    }

    auto self(std::static_pointer_cast<NetInput>(shared_from_this()));
    std::shared_ptr<helpers::bytebuf> buf;

    buf.swap(readbuf);
    if (buf) {
        buf->resize(read_buffer_size);
    } else {
        buf = std::make_shared<helpers::bytebuf>(read_buffer_size);
    }

    socket.async_read_some(boost::asio::buffer(*buf), [this, self, buf](const boost::system::error_code &ec, std::size_t len) {
        if (ec) {
            readbuf = buf;
            handle_error(ec);
        } else {
            buf->resize(len);
            parse_input(*buf);
            check_framing_errors();
            readbuf = buf;

            start_reading();
        }
    });
}
