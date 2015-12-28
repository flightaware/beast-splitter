#include <algorithm>
#include <boost/asio.hpp>

#include "beast_input.h"
#include "beast_message.h"

using namespace beastsplitter::input;
using namespace beastsplitter::message;

enum class SerialInput::ParserState { RESYNC, FIND_1A, READ_1A, READ_TYPE, READ_DATA, READ_ESCAPED_1A };

SerialInput::SerialInput(boost::asio::io_service &service_,
                         const std::string &path_,
                         Handler handler_,
                         ReceiverType fixed_receiver_type_,
                         const Settings &settings_,
                         unsigned int fixed_baud_rate_)
    : path(path_),
      port(service_),      
      handler(handler_),
      reconnect_timer(service_),
      fixed_receiver_type(fixed_receiver_type_),
      receiver_type(fixed_receiver_type_),
      settings(settings_),
    autobaud_interval(autobaud_base_interval),
    autobaud_timer(service_),
    good_sync(0),
    bad_sync(0),
    readbuf(read_buffer_size),
    state(ParserState::RESYNC)
{    
    // set up autobaud
    if (fixed_baud_rate_ == 0) {
        autobauding = true;

        // Start with the standard rates
        autobaud_rates.assign(autobaud_standard_rates.begin(), autobaud_standard_rates.end());

        // ensure that the port's current rate is the first rate that is tried
        boost::asio::serial_port_base::baud_rate current_rate;
        port.get_option(current_rate);

        auto i = std::find(autobaud_rates.begin(), autobaud_rates.end(), current_rate.value());
        if (i != autobaud_rates.end()) {
            autobaud_rates.erase(i);
        }

        autobaud_rates.insert(autobaud_rates.begin(), current_rate.value());
    } else {
        autobauding = false;
        autobaud_rates.push_back(fixed_baud_rate_);
    }

    baud_rate = autobaud_rates.begin();

    setup_port();
    start_reading();
}

void SerialInput::setup_port(void)
{
    boost::system::error_code ec;

    if (!port.is_open()) {
        port.open(path, ec);
        if (ec) {
            handle_error(ec);
            return;
        }
    }

    port.set_option(boost::asio::serial_port_base::character_size(8));
    port.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
    port.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
    port.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::hardware));
    port.set_option(boost::asio::serial_port_base::baud_rate(*baud_rate));

    send_settings_message();
}

void SerialInput::send_settings_message()
{
    // Construct settings message
    auto message = std::make_shared< std::array<uint8_t,16> >();
    *message = {
        0x1A, (uint8_t)'C', // Beast binary format
        0x1A, (uint8_t)(settings.filter_df11_df17_only ? 'D' : 'd'),
        0x1A, (uint8_t)'E', // AVRMLAT (unused in beast mode)
        0x1A, (uint8_t)(settings.crc_disable ? 'F' : 'f'),
        0x1A, (uint8_t)((settings.mask_df0_df4_df5 || receiver_type == ReceiverType::RADARCAPE) ? 'G' : 'g'),
        0x1A, (uint8_t)'I', // hardware handshake
        0x1A, (uint8_t)(settings.fec_disable ? 'I' : 'i'),
        0x1A, (uint8_t)(settings.modeac ? 'J' : 'j')
    };

    // Make sure we keep the shared_ptr alive by binding it.
    auto self(shared_from_this());
    boost::asio::async_write(port, boost::asio::buffer(*message),
                             [this,self,message] (boost::system::error_code ec, std::size_t len) {
                                 if (ec)
                                     handle_error(ec);
                             });
}

void SerialInput::change_settings(const Settings &newsettings)
{
    bool changed =
        (settings.filter_df11_df17_only != newsettings.filter_df11_df17_only) ||
        (settings.crc_disable != newsettings.crc_disable) ||
        (settings.mask_df0_df4_df5 != newsettings.mask_df0_df4_df5 && receiver_type != ReceiverType::RADARCAPE) ||
        (settings.fec_disable != newsettings.fec_disable) ||
        (settings.modeac != newsettings.modeac);

    settings = newsettings;
    if (changed && port.is_open())
        send_settings_message();
}

void SerialInput::handle_error(const boost::system::error_code &ec)
{
    std::cerr << "got error " << ec.message() << std::endl;
    if (ec == boost::asio::error::operation_aborted)
        return;

    autobaud_timer.cancel();
    if (port.is_open()) {
        boost::system::error_code ignored;
        port.close(ignored);
    }

    // reset autobaud state
    if (!autobaud_rates.empty()) {
        autobauding = true;
        autobaud_interval = autobaud_base_interval;
        baud_rate = autobaud_rates.begin();
    }

    // reset receiver type
    receiver_type = fixed_receiver_type;

    // schedule reconnect.
    auto self(shared_from_this());
    reconnect_timer.expires_from_now(reconnect_interval);
    reconnect_timer.async_wait([this,self] (const boost::system::error_code &ec) {
            if (!ec) {
                setup_port();
            }
        });    
}

void SerialInput::advance_autobaud(void)
{
    if (!autobauding)
        return;

    if (++baud_rate == autobaud_rates.end()) {
        // Ran out of rates to try. Increase the interval and start again
        baud_rate = autobaud_rates.begin();
        autobaud_interval = std::min(autobaud_max_interval, autobaud_interval * 2);
    }

    setup_port();

    auto self(shared_from_this());
    autobaud_timer.expires_from_now(autobaud_interval);
    autobaud_timer.async_wait([this,self] (const boost::system::error_code &ec) {
            if (!ec) {
                advance_autobaud();
            }
        });    
}

void SerialInput::start_reading(void)
{
    auto self(shared_from_this());

    readbuf.resize(readbuf.capacity());
    port.async_read_some(boost::asio::buffer(readbuf),
                         [this,self] (const boost::system::error_code &ec, std::size_t len) {
                             if (ec) {
                                 handle_error(ec);
                             } else {
                                 readbuf.resize(len);
                                 parse_input();
                                 start_reading();
                             }
                         });
}

void SerialInput::lost_sync(void)
{
    if (good_sync < 5) {
        ++bad_sync;
    } else {
        bad_sync = 0;
    }

    state = ParserState::RESYNC;
    good_sync = 0;
    bytes_since_sync = 0;

    if (!autobauding && autobaud_rates.size() > 1 && bad_sync > 50) {
        // We picked a rate, but it's not really working, let's
        // restart autobauding.
        autobauding = true;
        advance_autobaud();
    }
}

void SerialInput::parse_input()
{
    auto p = readbuf.begin();

    while (p != readbuf.end()) {
        switch (state) {
        case ParserState::RESYNC:
            // Scanning for <not-1A> <1A> <typebyte> <data...>
            for (; p != readbuf.end(); ++p) {
                if (*p != 0x1A) {
                    state = ParserState::FIND_1A;
                    break;
                }

                if (++bytes_since_sync > max_bytes_without_sync) {
                    // provoke a lost_sync() periodically
                    // while we do not have sync
                    lost_sync();
                    break;
                }
            }
            break;

        case ParserState::FIND_1A:
            // Scanning for <1A> <typebyte> <data...>
            for (; p != readbuf.end(); ++p) {
                if (*p == 0x1A) {
                    state = ParserState::READ_TYPE;
                    break;
                }

                if (++bytes_since_sync > max_bytes_without_sync) {
                    // provoke a lost_sync() periodically
                    // while we do not have sync
                    lost_sync();
                    break;
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
            }
            break;

        case ParserState::READ_TYPE:
            // Expecting <typebyte> <data...>
            messagetype = messagetype_from_byte(*p);
            if (messagetype == MessageType::INVALID) {
                lost_sync();
            } else {
                messagedata.clear();
                state = ParserState::READ_DATA;
                ++p;
            }
            break;

        case ParserState::READ_DATA:
            {
                // Reading message contents
                std::size_t msglen = messagetype_length(messagetype);
                while (p != readbuf.end() && messagedata.size() < msglen) {
                    uint8_t b = *p++;
                    if (b == 0x1A) {
                        if (p == readbuf.end()) {
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

                    messagedata.push_back(b);
                }

                if (messagedata.size() >= msglen) {
                    // Done with this message.
                    dispatch_message();
                    state = ParserState::READ_1A;
                }
            }
            break;

        case ParserState::READ_ESCAPED_1A:
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
            messagedata.push_back(*p++);
            if (messagedata.size() >= messagetype_length(messagetype)) {
                dispatch_message();
                state = ParserState::READ_1A;
            } else {
                state = ParserState::READ_DATA;
            }

            break;
            
        default:
            // WAT
            lost_sync();
            break;
        }
    }
}

void SerialInput::dispatch_message()
{
    ++good_sync;
    if (good_sync > autobaud_good_syncs_needed) {
        good_sync = autobaud_good_syncs_needed;
        bad_sync = 0;

        if (autobauding) {
            // We are autobauding and this rate looks good.
            autobauding = false;
            bad_sync = 0;
            autobaud_timer.cancel();
        }
    }

    // if we are not convinced of this rate, don't process messages yet
    if (autobauding)
        return;

    // if we have not detected the receiver type, watch for radarcape status messages
    // which is the only way to distinguish Beast vs Radarcape.
    if (receiver_type == ReceiverType::AUTO && messagetype == MessageType::STATUS) {
        receiver_type = ReceiverType::RADARCAPE;
        send_settings_message(); // for the g/G setting
    }

    // basic decoding, then pass it on.
    std::uint64_t timestamp =
        ((std::uint64_t)messagedata[0] << 40) |
        ((std::uint64_t)messagedata[1] << 32) |
        ((std::uint64_t)messagedata[2] << 24) | 
        ((std::uint64_t)messagedata[3] << 16) |
        ((std::uint64_t)messagedata[4] << 8) |
        ((std::uint64_t)messagedata[5]);

    std::uint8_t signal =
        messagedata[6];

    // dispatch it
    Message msg = {
        messagetype,
        timestamp,
        signal,
        std::vector<std::uint8_t>(messagedata.begin() + 7, messagedata.end())
    };

    handler(msg);
}
