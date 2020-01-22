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
#include <boost/asio.hpp>
#include <iomanip>
#include <iostream>

#include "beast_input.h"
#include "modes_message.h"

using namespace beast;

enum class BeastInput::ParserState { RESYNC, READ_1A, READ_TYPE, READ_DATA, READ_ESCAPED_1A };

BeastInput::BeastInput(boost::asio::io_service &service_, const Settings &fixed_settings_, const modes::Filter &filter_) : receiver_type(ReceiverType::UNKNOWN), fixed_settings(fixed_settings_), filter(filter_), receiving_gps_timestamps(false), autodetect_timer(service_), reconnect_timer(service_), liveness_timer(service_), good_sync(false), good_messages_count(0), bad_bytes_count(0), first_message(true), state(ParserState::RESYNC) {}

void BeastInput::start() { try_to_connect(); }

void BeastInput::close() {
    good_sync = false;
    disconnect();
}

void BeastInput::connection_established() {
    auto self(shared_from_this());

    first_message = true;
    receiving_gps_timestamps = false;
    good_sync = false;
    good_messages_count = 0;
    bad_bytes_count = 0;
    state = ParserState::READ_1A;
    current_settings = Settings();

    autodetect_timer.cancel();
    if (fixed_settings.radarcape.on())
        receiver_type = ReceiverType::RADARCAPE;
    else if (fixed_settings.radarcape.off())
        receiver_type = ReceiverType::BEAST;
    else {
        receiver_type = ReceiverType::UNKNOWN;
        autodetect_timer.expires_from_now(radarcape_detect_interval);
        autodetect_timer.async_wait([this, self](const boost::system::error_code &ec) {
            if (!ec) {
                receiver_type = ReceiverType::BEAST;
                send_settings_message();
            }
        });
    }

    send_settings_message();
}

void BeastInput::connection_failed() {
    good_sync = false;
    autodetect_timer.cancel();

    // schedule reconnect.
    auto self(shared_from_this());
    reconnect_timer.expires_from_now(reconnect_interval);
    reconnect_timer.async_wait([this, self](const boost::system::error_code &ec) {
        if (!ec) {
            try_to_connect();
        }
    });
}

void BeastInput::send_settings_message() {
    // apply fixed settings, let the filter set anything else that's not fixed
    Settings settings = fixed_settings | Settings(filter);

    // some hardcoded things we expect
    settings.radarcape = (receiver_type == ReceiverType::RADARCAPE);
    settings.binary_format = true;

    // subclass-specific settings
    apply_connection_settings(settings);

    // did anything actually change?
    if (settings == current_settings)
        return;

    // send it
    auto message = std::make_shared<helpers::bytebuf>(settings.to_message());
    if (low_level_write(message)) {
        std::cerr << what() << ": configured with settings: " << settings << std::endl;
        current_settings = settings;
    }
}

void BeastInput::set_filter(const modes::Filter &newfilter) {
    if (filter != newfilter) {
        filter = newfilter;
        send_settings_message();
    }
}

void BeastInput::parse_input(const helpers::bytebuf &buf) {
    auto p = buf.begin();
    auto last_good_message_end = p;

    while (p != buf.end()) {
        switch (state) {
        case ParserState::RESYNC:
            // Scanning for <not-1A> <1A> <typebyte> <data...>
            for (; p != buf.end(); ++p) {
                if (*p != 0x1A) {
                    auto q = p + 1;
                    if (q == buf.end()) {
                        // can't decide yet
                        state = ParserState::READ_1A;
                        p = q;
                        break;
                    }

                    if (*q == 0x1A) {
                        state = ParserState::READ_TYPE;
                        p = q + 1;
                        break;
                    }
                }
            }

            break;

        case ParserState::READ_1A:
            // Expecting <1A> <typebyte> <data...>
            if (*p == 0x1A) {
                state = ParserState::READ_TYPE;
                ++p;
            } else {
                lost_sync();
                break;
            }
            break;

        case ParserState::READ_TYPE:
            // Expecting <typebyte> <data...>
            messagetype = messagetype_from_byte(*p);
            if (messagetype == modes::MessageType::INVALID) {
                lost_sync();
                break;
            } else {
                metadata.clear();
                messagedata.clear();
                state = ParserState::READ_DATA;
                ++p;
            }

            break;

        case ParserState::READ_DATA: {
            // Reading message contents
            std::size_t msglen = modes::message_size(messagetype);
            while (p != buf.end() && messagedata.size() < msglen) {
                uint8_t b = *p++;
                if (b == 0x1A) {
                    if (p == buf.end()) {
                        // Can't handle it this time around.
                        state = ParserState::READ_ESCAPED_1A;
                        break;
                    }

                    if (*p != 0x1A) {
                        lost_sync();
                        break;
                    }

                    // valid 1A escape, consume it
                    ++p;
                }

                if (metadata.size() < 7)
                    metadata.push_back(b);
                else
                    messagedata.push_back(b);
            }

            if (messagedata.size() >= msglen) {
                // Done with this message.
                saw_good_message();
                last_good_message_end = p;
                dispatch_message();
                state = ParserState::READ_1A;
            }
        } break;

        case ParserState::READ_ESCAPED_1A: {
            // This happens if we see a 1A as the final
            // byte of a read; READ_DATA cannot handle
            // the escape immediately and sets state to
            // READ_ESCAPED_1A to handle the first
            // byte of the next read.

            if (*p != 0x1A) {
                lost_sync();
                break;
            }

            // valid 1A escape
            if (metadata.size() < 7)
                metadata.push_back(*p++);
            else
                messagedata.push_back(*p++);

            if (messagedata.size() >= modes::message_size(messagetype)) {
                saw_good_message();
                last_good_message_end = p;
                dispatch_message();
                state = ParserState::READ_1A;
            } else {
                state = ParserState::READ_DATA;
            }
        } break;

        default:
            // WAT
            lost_sync();
            break;
        }
    }

    if (!good_sync) {
        bad_bytes_count += (buf.end() - last_good_message_end);
    }
}

void BeastInput::saw_good_message() {
    good_sync = true;
    ++good_messages_count;
    bad_bytes_count = 0;
}

bool BeastInput::can_dispatch() const { return (receiver_type != ReceiverType::UNKNOWN); }

void BeastInput::lost_sync() {
    good_messages_count = 0;
    good_sync = false;
    state = ParserState::RESYNC;
}

void BeastInput::dispatch_message() {
    // monitor status messages for GPS timestamp bit
    // and for radarcape autodetection
    if (messagetype == modes::MessageType::STATUS) {
        receiving_gps_timestamps = Settings(messagedata[0]).gps_timestamps.on();
        if (receiver_type != ReceiverType::RADARCAPE) {
            receiver_type = ReceiverType::RADARCAPE;
            autodetect_timer.cancel();
            send_settings_message(); // for the g/G setting
        }

        auto self(shared_from_this());
        liveness_timer.expires_from_now(radarcape_liveness_interval);
        liveness_timer.async_wait([this, self](const boost::system::error_code &ec) {
            if (!ec) {
                std::cerr << what() << ": no recent status messages received" << std::endl;
                disconnect();
                connection_failed();
            }
        });
    }

    if (!can_dispatch())
        return;

    if (first_message) {
        first_message = false;
        std::cerr << what() << ": connected to a " << (receiver_type == ReceiverType::RADARCAPE ? "Radarcape" : "Beast") << "-style receiver" << std::endl;
    }

    if (!message_notifier)
        return;

    // basic decoding, then pass it on.
    std::uint64_t timestamp = 0;
    std::uint8_t signal = 0;

    if (messagetype == modes::MessageType::POSITION) {
        // position messages are special, they use the metadata area for actual data
        // so glue the metadata bytes onto the start of the data bytes and don't decode
        // timestamp/signal
        messagedata.insert(messagedata.begin(), metadata.cbegin(), metadata.cend());
    } else {
        timestamp = ((std::uint64_t)metadata[0] << 40) | ((std::uint64_t)metadata[1] << 32) | ((std::uint64_t)metadata[2] << 24) | ((std::uint64_t)metadata[3] << 16) | ((std::uint64_t)metadata[4] << 8) | ((std::uint64_t)metadata[5]);

        signal = metadata[6];
    }

    // dispatch it
    message_notifier(modes::Message(messagetype, receiving_gps_timestamps ? modes::TimestampType::GPS : modes::TimestampType::TWELVEMEG, timestamp, signal, std::move(messagedata)));
    messagedata.clear(); // make sure we leave it in a valid state after moving
}
