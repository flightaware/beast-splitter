// -*- c++ -*-

#ifndef BEAST_OUTPUT_H
#define BEAST_OUTPUT_H

#include <memory>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

#include "beast_message.h"

namespace beastsplitter {
    namespace output {
        // Settings requested by the peer
        struct Settings {
            Settings()
                : binary_format(true),
                  filter_df11_df17_only(false),
                  mlat_info(true),
                  crc_disable(false),
                  mask_df0_df4_df5(false),
                  rts_handshake(true),
                  fec_disable(false),
                  modeac(false)
            {}

            bool radarcape;
            bool binary_format;
            bool filter_df11_df17_only;
            bool mlat_info;
            bool crc_disable;
            bool mask_df0_df4_df5;
            bool gps_timestamps;
            bool rts_handshake;
            bool fec_disable;
            bool modeac;

            static Settings from_status_byte(std::uint8_t b) {
                // always radarcape mode
                Settings s;
                s.radarcape = true;
                s.binary_format = (b & 0x01) != 0;
                s.filter_df11_df17_only = (b & 0x02) != 0;
                s.mlat_info = (b & 0x04) != 0;
                s.crc_disable = (b & 0x08) != 0;
                s.gps_timestamps = (b & 0x10) != 0;
                s.rts_handshake = (b & 0x20) != 0;
                s.fec_disable = (b & 0x40) != 0;
                s.modeac = (b & 0x80) != 0;
                return s;
            }

            std::uint8_t to_status_byte() const {
                // always radarcape mode
                return
                    (binary_format ? 0x01 : 0x00) |
                    (filter_df11_df17_only ? 0x02 : 0x00) |
                    (mlat_info ? 0x04 : 0x00) |
                    (crc_disable ? 0x08 : 0x00) |
                    (gps_timestamps ? 0x10 : 0x00) |
                    (rts_handshake ? 0x20 : 0x00) |
                    (fec_disable ? 0x40 : 0x00) |
                    (modeac ? 0x80 : 0x00);
            }
        };

        class SocketOutput : public std::enable_shared_from_this<SocketOutput> {
        public:
            typedef std::shared_ptr<SocketOutput> pointer;

            // factory method, this class must always be constructed via make_shared
            static pointer create(boost::asio::io_service &service_,
                                  boost::asio::ip::tcp::socket &socket_,
                                  const Settings &settings_ = Settings())
            {
                return pointer(new SocketOutput(service_, socket_, settings_));
            }

            void start();
            void close();

            void set_settings_notifier(std::function<void(const Settings&)> notifier) {
                settings_notifier = notifier;
            }

            void set_close_notifier(std::function<void()> notifier) {
                close_notifier = notifier;
            }

            void dispatch_message(const beastsplitter::message::Message &message);

        private:
            SocketOutput(boost::asio::io_service &service_,
                         boost::asio::ip::tcp::socket &socket_,
                         const Settings &settings_);

            void read_command();
            void handle_command(std::vector<std::uint8_t> data);
            void handle_option_command(uint8_t option);
            void handle_error(const boost::system::error_code &ec);
            void reset_status_message_timer(std::chrono::milliseconds delay);
            void send_synthetic_status_message();
            
            void handle_message(const beastsplitter::message::Message &message);

            void write_message(beastsplitter::message::MessageType type,
                               std::uint64_t timestamp,
                               std::uint8_t signal,
                               const std::vector<std::uint8_t> &data);

            void write_binary_message(beastsplitter::message::MessageType type,
                                      std::uint64_t timestamp,
                                      std::uint8_t signal,
                                      const std::vector<std::uint8_t> &data);

            void write_avrmlat_message(std::uint64_t timestamp,
                                       const std::vector<std::uint8_t> &data);

            void write_avr_message(const std::vector<std::uint8_t> &data);
            
            template <class T> void socket_write(std::shared_ptr<T> msg);

            boost::asio::ip::tcp::socket socket;
            boost::asio::steady_timer status_message_timer;

            enum class ParserState;
            ParserState state;
            bool receiving_gps_timestamps;

            Settings settings;
            std::uint64_t last_message_timestamp;
            std::chrono::steady_clock::time_point last_message_clock;

            std::function<void(const Settings&)> settings_notifier;
            std::function<void()> close_notifier;
        };

        class SocketListener : public std::enable_shared_from_this<SocketListener> {
        public:
            typedef std::shared_ptr<SocketListener> pointer;

            // factory method, this class must always be constructed via make_shared
            static pointer create(boost::asio::io_service &service,
                                  boost::asio::ip::tcp::endpoint &endpoint,
                                  const Settings &settings)
            {
                return pointer(new SocketListener(service, endpoint, settings));
            }

            void start();
            void close();

            void dispatch_message(const beastsplitter::message::Message &message);

            void set_settings_notifier(std::function<void(const Settings&)> notifier) {
                settings_notifier = notifier;
            }


        private:
            SocketListener(boost::asio::io_service &service_, boost::asio::ip::tcp::endpoint &endpoint_,
                           const Settings &settings_);

            void accept_connection();

            boost::asio::io_service &service;
            boost::asio::ip::tcp::acceptor acceptor;
            boost::asio::ip::tcp::socket socket;
            Settings settings;
            std::function<void(const Settings&)> settings_notifier;

            std::vector< std::weak_ptr<SocketOutput> > connections;
        };
    };
};

#endif
