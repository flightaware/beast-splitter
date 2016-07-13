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

#include <iomanip>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ip/v6_only.hpp>

#include "beast_output.h"
#include "modes_message.h"

namespace asio = boost::asio;
using boost::asio::ip::tcp;

namespace beast {
    enum class SocketOutput::ParserState { FIND_1A, READ_1, READ_OPTION };

    SocketOutput::SocketOutput(asio::io_service &service_,
                               tcp::socket &&socket_,
                               const Settings &settings_)
        : service(service_),
          socket(std::move(socket_)),
          peer(socket.remote_endpoint()),
          state(ParserState::FIND_1A),
          settings(settings_),
          flush_pending(false)
    {
    }

    void SocketOutput::start()
    {
        read_commands();
    }

    void SocketOutput::read_commands()
    {
        auto self(shared_from_this());
        auto buf = std::make_shared< std::vector<std::uint8_t> >(512);

        socket.async_read_some(asio::buffer(*buf),
                               [this,self,buf] (const boost::system::error_code &ec, std::size_t len) {
                                   if (ec) {
                                       handle_error(ec);
                                   } else {
                                       buf->resize(len);
                                       process_commands(*buf);
                                       read_commands();
                                   }
                               });
    }

    void SocketOutput::process_commands(std::vector<std::uint8_t> data)
    {
        bool got_a_command = false;

        for (auto p = data.begin(); p != data.end(); ++p) {
            switch (state) {
            case ParserState::FIND_1A:
                if (*p == 0x1A)
                    state = ParserState::READ_1;
                break;

            case ParserState::READ_1:
                if (*p == 0x31)
                    state = ParserState::READ_OPTION;
                else
                    state = ParserState::FIND_1A;
                break;

            case ParserState::READ_OPTION:
                process_option_command(*p);
                got_a_command = true;
                state = ParserState::FIND_1A;
                break;
            }
        }

        if (got_a_command) {
            // just do this once at the end, not on every command
            std::cerr << peer << ": settings changed to " << settings << std::endl;
            if (settings_notifier)
                settings_notifier(settings);
        }
    }

    void SocketOutput::process_option_command(uint8_t option)
    {
        char ch = (char) option;
        switch (ch) {
        case 'c':
        case 'C':
            settings.binary_format = (ch == 'C');
            break;
        case 'd':
        case 'D':
            settings.filter_11_17_18 = (ch == 'D');
            break;
        case 'e':
        case 'E':
            settings.avrmlat = (ch == 'E');
            break;
        case 'f':
        case 'F':
            settings.crc_disable = (ch == 'F');
            break;
        case 'g':
        case 'G':
            if (settings.radarcape)
                settings.gps_timestamps = (ch == 'G');
            else
                settings.filter_0_4_5 = (ch == 'G');
            break;
        case 'h':
        case 'H':
            settings.rts_handshake = (ch == 'H');
            break;
        case 'i':
        case 'I':
            settings.fec_disable = (ch == 'I');
            break;
        case 'j':
        case 'J':
            settings.modeac_enable = (ch == 'J');
            break;
        default:
            // unrecognized
            return;
        }
    }

    void SocketOutput::write(const modes::Message &message)
    {
        if (!socket.is_open())
            return; // we are shut down

        if (message.type() == modes::MessageType::STATUS) {
            // local connection settings override the upstream data
            Settings used = settings | Settings(message.data()[0]);

            auto copy = message.data();
            copy[0] = used.to_status_byte();
            write_message(message.type(),
                          message.timestamp_type(),
                          message.timestamp(),
                          message.signal(),
                          copy);
        } else {
            write_message(message.type(),
                          message.timestamp_type(),
                          message.timestamp(),
                          message.signal(),
                          message.data());
        }
    }

    void SocketOutput::write_message(modes::MessageType type,
                                     modes::TimestampType timestamp_type,
                                     std::uint64_t timestamp,
                                     std::uint8_t signal,
                                     const helpers::bytebuf &data)
    {
        if (timestamp_type == modes::TimestampType::TWELVEMEG && !settings.radarcape.off() && settings.gps_timestamps.on()) {
            // GPS timestamps were explicitly requested
            // scale 12MHz to pseudo-GPS
            std::uint64_t ns = timestamp * 1000ULL / 12ULL;
            std::uint64_t seconds = (ns / 1000000000ULL) % 86400;
            std::uint64_t nanos = ns % 1000000000ULL;
            timestamp = (seconds << 30) | nanos;
        } else if (timestamp_type == modes::TimestampType::GPS && (settings.radarcape.off() || settings.gps_timestamps.off())) {
            // beast output or 12MHz timestamps were explicitly requested
            // scale GPS to 12MHz
            std::uint64_t seconds = timestamp >> 30;
            std::uint64_t nanos = timestamp & 0x3FFFFFFF;
            std::uint64_t ns = seconds * 1000000000ULL + nanos;
            timestamp = ns * 12ULL / 1000ULL;
        }

        // if gps_timestamps is DONTCARE, we just use whatever is provided

        if (settings.binary_format) {
            write_binary(type, timestamp, signal, data);
        } else if (settings.avrmlat) {
            if (type != modes::MessageType::STATUS)
                write_avrmlat(timestamp, data);
        } else {
            if (type != modes::MessageType::STATUS)
                write_avr(data);
        }
    }

    void SocketOutput::prepare_write()
    {
        if (!outbuf) {
            outbuf = std::make_shared<helpers::bytebuf>();
            outbuf->reserve(read_buffer_size);
        }
    }

    void SocketOutput::complete_write()
    {
        if (!flush_pending && !outbuf->empty()) {
            flush_pending = true;
            service.post(std::bind(&SocketOutput::flush_outbuf, shared_from_this()));
        }
    }

    void SocketOutput::flush_outbuf()
    {
        if (!outbuf || outbuf->empty())
            return;

        std::shared_ptr<helpers::bytebuf> writebuf;
        writebuf.swap(outbuf);

        auto self(shared_from_this());
        async_write(socket, boost::asio::buffer(*writebuf),
                    [this,self,writebuf] (const boost::system::error_code &ec, size_t len) {
                        // NB: we only reset the pending flag here,
                        // because async_write is a composed operation
                        // that might take a while to complete, and
                        // if we do another write before it completes
                        // then it might interleave data.
                        flush_pending = false;

                        if (!outbuf) {
                            writebuf->clear();
                            outbuf = writebuf;
                        }

                        if (ec)
                            handle_error(ec);
                    });
    }

    static inline void push_back_beast(helpers::bytebuf &v, std::uint8_t b)
    {
        if (b == 0x1A)
            v.push_back(0x1A);
        v.push_back(b);
    }

    void SocketOutput::write_binary(modes::MessageType type,
                                    std::uint64_t timestamp,
                                    std::uint8_t signal,
                                    const helpers::bytebuf &data)
    {
        prepare_write();
        outbuf->push_back(0x1A);
        outbuf->push_back(messagetype_to_byte(type));

        push_back_beast(*outbuf, (timestamp >> 40) & 0xFF);
        push_back_beast(*outbuf, (timestamp >> 32) & 0xFF);
        push_back_beast(*outbuf, (timestamp >> 24) & 0xFF);
        push_back_beast(*outbuf, (timestamp >> 16) & 0xFF);
        push_back_beast(*outbuf, (timestamp >> 8) & 0xFF);
        push_back_beast(*outbuf, timestamp & 0xFF);
        push_back_beast(*outbuf, signal);

        for (auto b : data)
            push_back_beast(*outbuf, b);

        complete_write();
    }

    // we could use ostrstream here, I guess, but this is simpler

    static inline void push_back_hex(helpers::bytebuf &v, std::uint8_t b)
    {
        static const char *hexdigits = "0123456789ABCDEF";
        v.push_back((std::uint8_t) hexdigits[(b >> 4) & 0x0F]);
        v.push_back((std::uint8_t) hexdigits[b & 0x0F]);
    }

    void SocketOutput::write_avr(const helpers::bytebuf &data)
    {
        prepare_write();

        outbuf->push_back((std::uint8_t) '*');
        for (auto b : data)
            push_back_hex(*outbuf, b);
        outbuf->push_back((std::uint8_t) ';');
        outbuf->push_back((std::uint8_t) '\n');

        complete_write();
    }

    void SocketOutput::write_avrmlat(std::uint64_t timestamp, const helpers::bytebuf &data)
    {
        prepare_write();

        outbuf->push_back((std::uint8_t) '@');
        push_back_hex(*outbuf, (timestamp >> 40) & 0xFF);
        push_back_hex(*outbuf, (timestamp >> 32) & 0xFF);
        push_back_hex(*outbuf, (timestamp >> 24) & 0xFF);
        push_back_hex(*outbuf, (timestamp >> 16) & 0xFF);
        push_back_hex(*outbuf, (timestamp >> 8) & 0xFF);
        push_back_hex(*outbuf, timestamp & 0xFF);
        for (auto b : data)
            push_back_hex(*outbuf, b);
        outbuf->push_back((std::uint8_t) ';');
        outbuf->push_back((std::uint8_t) '\n');

        complete_write();
    }

    void SocketOutput::handle_error(const boost::system::error_code &ec)
    {
        if (ec == boost::asio::error::eof) {
            std::cerr << peer << ": connection closed" << std::endl;
        } else if (ec != boost::asio::error::operation_aborted) {
            std::cerr << peer << ": connection error: " << ec.message() << std::endl;
        }

        close();
    }

    void SocketOutput::close()
    {
        socket.close();
        if (close_notifier)
            close_notifier();
    }

    //////////////

    SocketListener::SocketListener(asio::io_service &service_,
                                   const tcp::endpoint &endpoint_,
                                   modes::FilterDistributor &distributor_,
                                   const Settings &initial_settings_)
        : service(service_),
          acceptor(service_),
          endpoint(endpoint_),
          socket(service_),
          distributor(distributor_),
          initial_settings(initial_settings_)
    {
    }

    void SocketListener::start()
    {
        acceptor.open(endpoint.protocol());
        acceptor.set_option(asio::socket_base::reuse_address(true));
        acceptor.set_option(tcp::acceptor::reuse_address(true));

        // We are v6 aware and bind separately to v4 and v6 addresses
        if (endpoint.protocol() == tcp::v6())
            acceptor.set_option(asio::ip::v6_only(true));

        acceptor.bind(endpoint);
        acceptor.listen();
        accept_connection();
    }

    void SocketListener::close()
    {
        acceptor.cancel();
        socket.close();
    }

    void SocketListener::accept_connection()
    {
        auto self(shared_from_this());

        acceptor.async_accept(socket,
                              peer,
                              [this,self] (const boost::system::error_code &ec) {
                                  if (!ec) {
                                      std::cerr << endpoint << ": accepted a connection from " << peer << " with settings " << initial_settings << std::endl;
                                      SocketOutput::pointer new_output = SocketOutput::create(service, std::move(socket), initial_settings);

                                      modes::FilterDistributor::handle h = distributor.add_client(std::bind(&SocketOutput::write, new_output, std::placeholders::_1),
                                                                                                  initial_settings.to_filter());

                                      new_output->set_settings_notifier([this,self,h] (const Settings &newsettings) {
                                              distributor.update_client_filter(h, newsettings.to_filter());
                                          });

                                      new_output->set_close_notifier([this,self,h] {
                                              distributor.remove_client(h);
                                          });

                                      new_output->start();
                                  } else {
                                      if (ec == boost::system::errc::operation_canceled)
                                          return;
                                      std::cerr << endpoint << ": accept error: " << ec.message() << std::endl;
                                  }

                                  accept_connection();
                              });
    }

    //////////////

    SocketConnector::SocketConnector(asio::io_service &service_,
                                     const std::string &host_,
                                     const std::string &port_or_service_,
                                     modes::FilterDistributor &distributor_,
                                     const Settings &initial_settings_)
        : service(service_),
          resolver(service_),
          socket(service_),
          reconnect_timer(service_),
          host(host_),
          port_or_service(port_or_service_),
          distributor(distributor_),
          initial_settings(initial_settings_),
          running(false)
    {
    }

    void SocketConnector::start()
    {
        running = true;
        resolve_and_connect();
    }

    void SocketConnector::close()
    {
        running = false;
        resolver.cancel();
        reconnect_timer.cancel();
        socket.close();
    }

    void SocketConnector::resolve_and_connect(const boost::system::error_code &ec)
    {
        if (!running) {
            return;
        }

        if (ec) {
            assert (ec == boost::asio::error::operation_aborted);
            return;
        }

        auto self(shared_from_this());

        tcp::resolver::query query(host, port_or_service);
        resolver.async_resolve(query, [this,self] (const boost::system::error_code &ec,
                                                   tcp::resolver::iterator it) {
                                   if (!ec) {
                                       next_endpoint = it;
                                       try_next_endpoint();
                                   } else if (ec == boost::asio::error::operation_aborted) {
                                       return;
                                   } else {
                                       std::cerr << host << ":" << port_or_service << ": could not resolve address: " << ec.message() << std::endl;
                                       schedule_reconnect();
                                       return;
                                   }
                               });
    }

    void SocketConnector::try_next_endpoint()
    {
        if (!running) {
            return;
        }

        if (next_endpoint == tcp::resolver::iterator()) {
            // No more addresses to try
            schedule_reconnect();
            return;
        }

        tcp::endpoint endpoint = *next_endpoint++;

        auto self(shared_from_this());
        socket.async_connect(endpoint,
                             [this,self,endpoint] (const boost::system::error_code &ec) {
                                 if (!ec) {
                                     connection_established(endpoint);
                                 } else if (ec == boost::asio::error::operation_aborted) {
                                     return;
                                 } else {
                                     std::cerr << host << ":" << port_or_service << ": connection to " << endpoint << " failed: " << ec.message() << std::endl;
                                     socket.close();
                                     try_next_endpoint();
                                 }
                             });
    }

    void SocketConnector::schedule_reconnect()
    {
        if (running) {
            std::cerr << host << ":" << port_or_service << ": reconnecting in "
                      << std::chrono::duration_cast<std::chrono::seconds>(reconnect_interval).count() << " seconds"
                      << std::endl;

            auto self(shared_from_this());
            reconnect_timer.expires_from_now(reconnect_interval);
            reconnect_timer.async_wait(std::bind(&SocketConnector::resolve_and_connect, self, std::placeholders::_1));
        }
    }

    void SocketConnector::connection_established(const tcp::endpoint &endpoint)
    {
        auto self(shared_from_this());

        std::cerr << host << ":" << port_or_service << ": connected to " << endpoint << " with settings " << initial_settings << std::endl;
        SocketOutput::pointer new_output = SocketOutput::create(service, std::move(socket), initial_settings);

        modes::FilterDistributor::handle h = distributor.add_client(std::bind(&SocketOutput::write, new_output, std::placeholders::_1),
                                                                    initial_settings.to_filter());

        new_output->set_settings_notifier([this,self,h] (const Settings &newsettings) {
                distributor.update_client_filter(h, newsettings.to_filter());
            });

        new_output->set_close_notifier([this,self,h] {
                distributor.remove_client(h);
                schedule_reconnect();
            });

        new_output->start();
    }
};
