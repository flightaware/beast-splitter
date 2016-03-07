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

#include "helpers.h"
#include "beast_settings.h"
#include "modes_message.h"
#include "modes_filter.h"

namespace beast {
    inline modes::MessageType messagetype_from_byte(std::uint8_t b)
    {
        switch (b) {
        case 0x31: return modes::MessageType::MODE_AC;
        case 0x32: return modes::MessageType::MODE_S_SHORT;
        case 0x33: return modes::MessageType::MODE_S_LONG;
        case 0x34: return modes::MessageType::STATUS;
        default: return modes::MessageType::INVALID;
        }
    }

    enum class ReceiverType { UNKNOWN, BEAST, RADARCAPE };

    class SerialInput : public std::enable_shared_from_this<SerialInput> {
    public:
        typedef std::shared_ptr<SerialInput> pointer;

        // the standard baud rates to try, in their preferred order
        const std::array<unsigned int,5> autobaud_standard_rates { { 3000000, 1000000, 921600, 230400, 115200 } };

        // the initial interval to wait for (autobaud_good_syncs_needed) good messages before changing baud rates
        const std::chrono::milliseconds autobaud_base_interval = std::chrono::milliseconds(1000);

        // the maximum interval between changing baud rates
        const std::chrono::milliseconds autobaud_max_interval = std::chrono::milliseconds(16000);

        // the number of consecutive messages without sync errors needed before the baud rate is fixed
        const unsigned int autobaud_good_syncs_needed = 10;

        // the number of consecutive sync fails (without a "good" sync patch) before restarting autobauding
        const unsigned int autobaud_restart_after_bad_syncs = 20;

        // while waiting for sync, count an extra bad sync every how many bytes?
        const unsigned int max_bytes_without_sync = 30;

        // the number of bytes to try to read at a time from the serial port
        const size_t read_buffer_size = 4096;

        // how long to wait before trying to reopen the serial port after an error
        const std::chrono::milliseconds reconnect_interval = std::chrono::seconds(60);

        // how long to wait for a radarcape status message before assuming the receiver
        // isn't a radarcape
        const std::chrono::milliseconds radarcape_detect_interval = std::chrono::seconds(3);

        // how long to wait between scheduling reads (to reduce the spinning on short messages)
        const std::chrono::milliseconds read_interval = std::chrono::milliseconds(50);

        // message notifier type
        typedef std::function<void(const modes::Message &)> MessageNotifier;

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

        void start(void);
        void close(void);

        bool is_connected(void) const {
            return (!first_message);
        }

        ReceiverType receiver(void) const {
            return receiver_type;
        }

        // change the input filter to the given filter
        void set_filter(const modes::Filter &filter_);

        // change where received messages go to
        void set_message_notifier(MessageNotifier notifier) {
            message_notifier = notifier;
        }

    private:
        // construct a new serial input instance and start processing data
        SerialInput(boost::asio::io_service &service_,
                    const std::string &path_,
                    unsigned int fixed_baud_rate,
                    const Settings &fixed_settings_,
                    const modes::Filter &filter_);

        void send_settings_message(void);
        void handle_error(const boost::system::error_code &ec);
        void advance_autobaud(void);
        void start_reading(const boost::system::error_code &ec = boost::system::error_code());
        void lost_sync(void);
        void parse_input(const helpers::bytebuf &buf);
        void dispatch_message(void);

        // path to the serial device
        std::string path;

        // the port we're using
        boost::asio::serial_port port;

        // handler to call with deframed messages
        MessageNotifier message_notifier;

        // timer that expires after reconnect_interval
        boost::asio::steady_timer reconnect_timer;

        // the currently detected receiver type
        ReceiverType receiver_type;

        // settings that are always set, regardless of the filter state
        Settings fixed_settings;

        // the current input filter
        modes::Filter filter;

        // true if we are actively hunting for the correct baud rate
        bool autobauding;

        // true if we are receiving GPS timestamps
        bool receiving_gps_timestamps;

        // vector of baud rates to try, single entry if a fixed rate is set
        std::vector<unsigned int> autobaud_rates;

        // current iterator into autobaud_rates
        std::vector<unsigned int>::iterator autobaud_rate;

        // actual rate to use
        unsigned int baud_rate;

        // how long to wait between autobaud attempts; doubles (up to a limit)
        // each time all rates in autobaud_rates have been tried
        std::chrono::milliseconds autobaud_interval;

        // timer that expires after autobaud_interval
        boost::asio::steady_timer autobaud_timer;

        // timer that expires after autodetect_interval
        boost::asio::steady_timer autodetect_timer;

        // timer that expires when we want to read some more data
        boost::asio::steady_timer read_timer;

        // number of consecutive messages with good sync we have seen
        unsigned good_sync;

        // number of consecutive sync failures we have seen
        unsigned bad_sync;

        // bytes since we last had sync or reported bad sync
        unsigned bytes_since_sync;

        // are we still waiting for the first good message?
        bool first_message;

        // cached buffer used for reads
        std::shared_ptr<helpers::bytebuf> readbuf;

        // deframed message (possibly still being built)
        modes::MessageType messagetype;
        helpers::bytebuf metadata;
        helpers::bytebuf messagedata;

        // parser FSM state
        enum class ParserState;
        ParserState state;
    };
};

#endif
