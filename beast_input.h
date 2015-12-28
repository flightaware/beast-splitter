// -*- c++ -*-

#ifndef BEAST_INPUT_H
#define BEAST_INPUT_H

#include <cstdint>
#include <vector>
#include <chrono>
#include <memory>

#include <boost/asio/io_service.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/asio/steady_timer.hpp>

namespace beastsplitter {
    namespace message {
        class Message;
        enum class MessageType;
    };

    namespace input {
        // variable settings; the others (format, timestamp, etc) are fixed on the input side.
        struct Settings {
            Settings()
                : filter_df11_df17_only(false),
                  crc_disable(false),
                  mask_df0_df4_df5(false),
                  fec_disable(false),
                  modeac(false)
            {}
            
            bool filter_df11_df17_only;   // true: deliver only DF11/DF17
            bool crc_disable;             // true: no CRC checks
            bool mask_df0_df4_df5;        // beast only. true: don't deliver DF0/DF4/DF5
            bool fec_disable;             // true: no FEC
            bool modeac;                  // true: decode Mode A/C
        };

        // the type of receiver we are connected to
        enum class ReceiverType { AUTO, BEAST, RADARCAPE };
        
        class SerialInput : std::enable_shared_from_this<SerialInput> {
        public:
            // the standard baud rates to try, in their preferred order
            const std::array<unsigned int,5> autobaud_standard_rates { { 3000000, 1000000, 921600, 230400, 115200 } };
            // the initial interval to wait for good messages before changing baud rates
            const std::chrono::milliseconds autobaud_base_interval = std::chrono::milliseconds(500);
            // the maximum interval between changing baud rates
            const std::chrono::milliseconds autobaud_max_interval = std::chrono::milliseconds(15000);
            // the number of consecutive messages without sync errors needed before the baud rate is fixed
            const unsigned int autobaud_good_syncs_needed = 5;
            // the number of consecutive sync fails (without a "good" sync patch) before restarting autobauding
            const unsigned int autobaud_restart_after_bad_syncs = 50;
            // while waiting for sync, count an extra bad sync every how many bytes?
            const unsigned int max_bytes_without_sync = 30;

            // the number of bytes to try to read at a time from the serial port
            const size_t read_buffer_size = 4096;

            // how long to wait before trying to reopen the serial port after an error
            const std::chrono::milliseconds reconnect_interval = std::chrono::seconds(15);

            typedef std::function< void(const beastsplitter::message::Message &) > Handler;

            // construct a new serial input instance and start processing data
            SerialInput(boost::asio::io_service &service_,
                        const std::string &path_,
                        Handler handler_, 
                        ReceiverType fixed_receiver_type_ = ReceiverType::AUTO,
                        const Settings &settings_ = Settings(),
                        unsigned int fixed_baud_rate = 0);

            // change the input settings to the given settings
            void change_settings(const Settings &settings_);

        private:
            void setup_port(void);
            void send_settings_message(void);
            void handle_error(const boost::system::error_code &ec);
            void advance_autobaud(void);
            void start_reading(void);
            void lost_sync(void);
            void parse_input(void);
            void dispatch_message(void);

            // path to the serial device
            std::string path;
            // the port we're using
            boost::asio::serial_port port;
            // handler to call with deframed messages
            Handler handler;

            // timer that expires after reconnect_interval
            boost::asio::steady_timer reconnect_timer;

            // the fixed receiver type to us, or AUTO to autodetect
            ReceiverType fixed_receiver_type;
            // the currently detected receiver type
            ReceiverType receiver_type;
            // the current input settings
            Settings settings;

            // true if we are actively hunting for the correct baud rate
            bool autobauding;
            // vector of baud rates to try, single entry if a fixed rate is set
            std::vector<unsigned int> autobaud_rates;
            // currently used baud rate, iterator into autobaud_rates
            std::vector<unsigned int>::iterator baud_rate;
            // how long to wait between autobaud attempts; doubles (up to a limit)
            // each time all rates in autobaud_rates have been tried
            std::chrono::milliseconds autobaud_interval;
            // timer that expires after autobaud_interval
            boost::asio::steady_timer autobaud_timer;
            // number of consecutive messages with good sync we have seen
            unsigned good_sync;
            // number of consecutive sync failures we have seen
            unsigned bad_sync;
            // bytes since we last had sync or reported bad sync
            unsigned bytes_since_sync;

            // raw data from the serial port
            std::vector<std::uint8_t> readbuf;

            // deframed message (possibly still being built)
            beastsplitter::message::MessageType messagetype;
            std::vector<std::uint8_t> messagedata;

            // parser FSM state
            enum class ParserState;
            ParserState state;
        };
    };
};

#endif
