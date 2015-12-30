#include <iomanip>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include "beast_output.h"
#include "modes_message.h"

namespace beast {
    enum class SocketOutput::ParserState { FIND_1A, READ_1, READ_OPTION };

    SocketOutput::SocketOutput(boost::asio::io_service &service_,
                               boost::asio::ip::tcp::socket &&socket_,
                               const Settings &settings_)
        : socket(std::move(socket_)),
          state(ParserState::FIND_1A),
          settings(settings_)
    {
    }

    void SocketOutput::start()
    {
        std::cerr << "output: start" << std::endl;
        read_commands();
    }

    void SocketOutput::read_commands()
    {
        auto self(shared_from_this());
        auto buf = std::make_shared< std::vector<std::uint8_t> >(512);    

        async_read(socket, boost::asio::buffer(*buf),
                   [this,self,buf] (const boost::system::error_code &ec, std::size_t len) {
                       if (ec) {
                           handle_error(ec);
                       } else {
                           std::cerr << "output: handling commands" << std::endl;
                           process_commands(*buf);
                           read_commands();
                       }
                   });
    }

    void SocketOutput::process_commands(std::vector<std::uint8_t> data)
    {
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
                state = ParserState::FIND_1A;
                break;
            }
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

        if (settings_notifier)
            settings_notifier(settings);
    }

    void SocketOutput::write(const modes::Message &message)
    {
        if (!socket.is_open())
            return; // we are shut down

        if (message.type() == modes::MessageType::STATUS) {
            // put the connection-specific dipswitch settings in
            auto copy = message.data();
            copy[0] = settings.to_status_byte();
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
        if (timestamp_type == modes::TimestampType::TWELVEMEG && settings.gps_timestamps) {
            // scale 12MHz to GPS
            std::uint64_t ns = timestamp * 1000ULL / 12ULL;
            std::uint64_t seconds = (ns / 1000000000ULL) % 86400;
            std::uint64_t nanos = ns % 1000000000ULL;
            timestamp = (seconds << 30) | nanos;
        } else if (timestamp_type == modes::TimestampType::GPS && !settings.gps_timestamps) {
            // scale GPS to 12MHz
            std::uint64_t seconds = timestamp >> 30;
            std::uint64_t nanos = timestamp & 0x3FFFFFFF;
            std::uint64_t ns = seconds * 1000000000ULL + nanos;
            timestamp = ns * 12ULL / 1000ULL;
        }

        if (settings.binary_format)
            write_binary(type, timestamp, signal, data);
        else if (settings.avrmlat)
            write_avrmlat(timestamp, data);
        else
            write_avr(data);
    }

    static void push_back_escape(std::vector<std::uint8_t> &v, std::uint8_t b)
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
        // build a vector with the escaped data to write
        auto msg = std::make_shared< std::vector<uint8_t> >();
        msg->reserve(32);

        msg->push_back(0x1A);
        msg->push_back(messagetype_to_byte(type));

        push_back_escape(*msg, (timestamp >> 40) & 0xFF);
        push_back_escape(*msg, (timestamp >> 32) & 0xFF);
        push_back_escape(*msg, (timestamp >> 24) & 0xFF);
        push_back_escape(*msg, (timestamp >> 16) & 0xFF);
        push_back_escape(*msg, (timestamp >> 8) & 0xFF);
        push_back_escape(*msg, timestamp & 0xFF);
        push_back_escape(*msg, signal);

        for (auto b : data)
            push_back_escape(*msg, b);

        write_bytes(msg);
    }

    // we could use ostrstream here, I guess, but this is simpler

    static void push_back_hex(std::vector<char> &v, std::uint8_t b)
    {
        static const char *hexdigits = "0123456789ABCDEF";
        v.push_back(hexdigits[(b >> 4) & 0x0F]);
        v.push_back(hexdigits[b & 0x0F]);
    }

    void SocketOutput::write_avr(const helpers::bytebuf &data)
    {
        auto msg = std::make_shared< std::vector<char> >();
        msg->reserve(3 + data.size() * 2);
   
        msg->push_back('*');
        for (auto b : data)
            push_back_hex(*msg, b);
        msg->push_back(';');
        msg->push_back('\n');

        write_bytes(msg);
    }

    void SocketOutput::write_avrmlat(std::uint64_t timestamp, const helpers::bytebuf &data)
    {
        auto msg = std::make_shared< std::vector<char> >();
        msg->reserve(15 + data.size() * 2);

        msg->push_back('@');
        push_back_hex(*msg, (timestamp >> 40) & 0xFF);
        push_back_hex(*msg, (timestamp >> 32) & 0xFF);
        push_back_hex(*msg, (timestamp >> 24) & 0xFF);
        push_back_hex(*msg, (timestamp >> 16) & 0xFF);
        push_back_hex(*msg, (timestamp >> 8) & 0xFF);
        push_back_hex(*msg, timestamp & 0xFF);
        for (auto b : data)
            push_back_hex(*msg, b);
        msg->push_back(';');
        msg->push_back('\n');

        write_bytes(msg);
    }

    void SocketOutput::handle_error(const boost::system::error_code &ec)
    {
        std::cerr << "output: connection error seen: " << ec.message() << std::endl;
        close();
    }

    void SocketOutput::close()
    {
        socket.close();
        if (close_notifier)
            close_notifier();
    }    

    //////////////

    SocketListener::SocketListener(boost::asio::io_service &service_,
                                   boost::asio::ip::tcp::endpoint &endpoint_,
                                   modes::FilterDistributor &distributor_,
                                   const Settings &initial_settings_)
        : service(service_),
          acceptor(service_, endpoint_),
          socket(service_),
          distributor(distributor_),
          initial_settings(initial_settings_)
    {
    }

    void SocketListener::start()
    {
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

        std::cerr << "starting accept" << std::endl;
        acceptor.async_accept(socket,
                              [this,self] (const boost::system::error_code &ec) {
                                  if (!ec) {
                                      std::cerr << "accepted a connection" << std::endl;
                                      SocketOutput::pointer new_output = SocketOutput::create(service, std::move(socket), initial_settings);

                                      std::cerr << "Initial settings are " << initial_settings << std::endl;
                                      modes::FilterDistributor::handle h = distributor.add_client(std::bind(&SocketOutput::write, new_output, std::placeholders::_1),
                                                                                                  initial_settings.to_filter());

                                      new_output->set_settings_notifier([this,self,h] (const Settings &newsettings) {
                                              std::cerr << "Client updated settings to" << newsettings << std::endl;
                                              distributor.update_client_filter(h, newsettings.to_filter());
                                          });

                                      new_output->set_close_notifier([this,self,h] {
                                              distributor.remove_client(h);
                                          });

                                      new_output->start();

                                      accept_connection();
                                  }
                              });
    }

};
