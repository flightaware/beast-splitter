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

#ifndef BEAST_OUTPUT_H
#define BEAST_OUTPUT_H

#include <memory>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

#include "modes_message.h"
#include "beast_settings.h"

namespace beast {
    inline std::uint8_t messagetype_to_byte(modes::MessageType t)
    {
        switch (t) {
        case modes::MessageType::MODE_AC: return 0x31;
        case modes::MessageType::MODE_S_SHORT: return 0x32;
        case modes::MessageType::MODE_S_LONG: return 0x33;
        case modes::MessageType::STATUS: return 0x34;
        default: return 0;
        }
    }

    class SocketOutput : public std::enable_shared_from_this<SocketOutput> {
    public:
        typedef std::shared_ptr<SocketOutput> pointer;

        const unsigned int read_buffer_size = 4096;

        // factory method, this class must always be constructed via make_shared
        static pointer create(boost::asio::io_service &service,
                              boost::asio::ip::tcp::socket &&socket,
                              const Settings &settings = Settings())
        {
            return pointer(new SocketOutput(service, std::move(socket), settings));
        }

        void start();
        void close();

        void set_settings_notifier(std::function<void(const Settings&)> notifier) {
            settings_notifier = notifier;
        }

        void set_close_notifier(std::function<void()> notifier) {
            close_notifier = notifier;
        }

        void write(const modes::Message &message);

    private:
        SocketOutput(boost::asio::io_service &service_,
                     boost::asio::ip::tcp::socket &&socket_,
                     const Settings &settings_);

        void read_commands();
        void process_commands(std::vector<std::uint8_t> data);
        void process_option_command(uint8_t option);

        void handle_error(const boost::system::error_code &ec);

        void write_message(modes::MessageType type,
                           modes::TimestampType timestamp_type,
                           std::uint64_t timestamp,
                           std::uint8_t signal,
                           const helpers::bytebuf &data);

        void write_binary(modes::MessageType type,
                          std::uint64_t timestamp,
                          std::uint8_t signal,
                          const helpers::bytebuf &data);

        void write_avrmlat(std::uint64_t timestamp,
                           const helpers::bytebuf &data);

        void write_avr(const helpers::bytebuf &data);

        void prepare_write();
        void complete_write();
        void flush_outbuf();

        boost::asio::io_service &service;
        boost::asio::ip::tcp::socket socket;
        boost::asio::ip::tcp::endpoint peer;

        enum class ParserState;
        ParserState state;

        Settings settings;

        std::function<void(const Settings&)> settings_notifier;
        std::function<void()> close_notifier;

        std::shared_ptr<helpers::bytebuf> outbuf;
        bool flush_pending;
    };

    class SocketListener : public std::enable_shared_from_this<SocketListener> {
    public:
        typedef std::shared_ptr<SocketListener> pointer;

        // factory method, this class must always be constructed via make_shared
        static pointer create(boost::asio::io_service &service,
                              const boost::asio::ip::tcp::endpoint &endpoint,
                              modes::FilterDistributor &distributor,
                              const Settings &initial_settings)
        {
            return pointer(new SocketListener(service, endpoint, distributor, initial_settings));
        }

        void start();
        void close();

    private:
        SocketListener(boost::asio::io_service &service_, const boost::asio::ip::tcp::endpoint &endpoint_,
                       modes::FilterDistributor &distributor, const Settings &initial_settings_);

        void accept_connection();

        boost::asio::io_service &service;
        boost::asio::ip::tcp::acceptor acceptor;
        boost::asio::ip::tcp::endpoint endpoint;
        boost::asio::ip::tcp::socket socket;
        boost::asio::ip::tcp::endpoint peer;
        modes::FilterDistributor &distributor;
        Settings initial_settings;
    };

    class SocketConnector : public std::enable_shared_from_this<SocketConnector> {
    public:
        typedef std::shared_ptr<SocketConnector> pointer;

        const std::chrono::milliseconds reconnect_interval = std::chrono::seconds(60);

        // factory method, this class must always be constructed via make_shared
        static pointer create(boost::asio::io_service &service,
                              const std::string &host,
                              const std::string &port_or_service,
                              modes::FilterDistributor &distributor,
                              const Settings &initial_settings)
        {
            return pointer(new SocketConnector(service, host, port_or_service, distributor, initial_settings));
        }

        void start();
        void close();

    private:
        SocketConnector(boost::asio::io_service &service_,
                        const std::string &host_,
                        const std::string &port_or_service_,
                        modes::FilterDistributor &distributor,
                        const Settings &initial_settings_);

        void schedule_reconnect();
        void resolve_and_connect(const boost::system::error_code &ec = boost::system::error_code());
        void try_next_endpoint();
        void connection_established(const boost::asio::ip::tcp::endpoint &endpoint);


        boost::asio::io_service &service;
        boost::asio::ip::tcp::resolver resolver;
        boost::asio::ip::tcp::socket socket;
        boost::asio::steady_timer reconnect_timer;

        std::string host;
        std::string port_or_service;
        modes::FilterDistributor &distributor;
        Settings initial_settings;

        bool running;
        boost::asio::ip::tcp::resolver::iterator next_endpoint;
    };
};

#endif
