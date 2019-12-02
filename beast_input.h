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
        case 0x35: return modes::MessageType::POSITION;
        default: return modes::MessageType::INVALID;
        }
    }

    enum class ReceiverType { UNKNOWN, BEAST, RADARCAPE };

    class BeastInput : public std::enable_shared_from_this<BeastInput> {
    public:
        typedef std::shared_ptr<BeastInput> pointer;

        // how long to wait before trying to reopen the connection after an error
        const std::chrono::milliseconds reconnect_interval = std::chrono::seconds(60);

        // how long to wait for a radarcape status message before assuming the receiver
        // isn't a radarcape
        const std::chrono::milliseconds radarcape_detect_interval = std::chrono::seconds(3);

        // how long to wait for a radarcape status message (when in radarcape mode)
        // before assuming the connection is dead
        const std::chrono::milliseconds radarcape_liveness_interval = std::chrono::seconds(15);

        // message notifier type
        typedef std::function<void(const modes::Message &)> MessageNotifier;

        void start(void);
        void close(void);

        bool is_connected(void) const {
            return (good_sync && receiver_type != ReceiverType::UNKNOWN);
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

    protected:
        // construct a new input instance
        BeastInput(boost::asio::io_service &service_,
                   const Settings &fixed_settings_,
                   const modes::Filter &filter_);

        virtual ~BeastInput() {}

        void connection_established();
        void connection_failed();
        void parse_input(const helpers::bytebuf &buf);
        bool have_good_sync() const { return good_sync; }
        unsigned good_messages() const { return good_messages_count; }
        unsigned bad_bytes() const { return bad_bytes_count; }

        virtual void saw_good_message(void);
        virtual bool can_dispatch(void) const;

        virtual std::string what() const = 0;
        virtual void try_to_connect() = 0;
        virtual void disconnect() = 0;
        virtual bool low_level_write(std::shared_ptr<helpers::bytebuf> message) = 0;
        virtual void apply_connection_settings(Settings &settings) {}

    private:
        void send_settings_message(void);
        void lost_sync(void);
        void dispatch_message(void);

        // handler to call with deframed messages
        MessageNotifier message_notifier;

        // the currently detected receiver type
        ReceiverType receiver_type;

        // settings that are always set, regardless of the filter state
        Settings fixed_settings;

        // the current input filter
        modes::Filter filter;

        // true if we are receiving GPS timestamps
        bool receiving_gps_timestamps;

        // timer that expires after autodetect_interval
        boost::asio::steady_timer autodetect_timer;

        // timer that expires after reconnect_interval
        boost::asio::steady_timer reconnect_timer;

        // timer that expires after radarcape_liveness_interval
        boost::asio::steady_timer liveness_timer;

        // are we currently in sync?
        bool good_sync;

        // number of consecutive messages with good sync we have seen
        unsigned good_messages_count;

        // bytes since we last had sync or reported bad sync
        unsigned bad_bytes_count;

        // are we still waiting for the first good message?
        bool first_message;

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
