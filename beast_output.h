// -*- c++ -*-

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
};

#endif
