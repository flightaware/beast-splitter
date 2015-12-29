#include <iomanip>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include "beast_output.h"
#include "beast_message.h"

using namespace beastsplitter::output;

enum class SocketOutput::ParserState { FIND_1A, READ_1, READ_OPTION };

SocketOutput::SocketOutput(boost::asio::io_service &service_,
                           boost::asio::ip::tcp::socket &socket_,
                           const Settings &settings_)
    : socket(std::move(socket_)),
      status_message_timer(service_),
      state(ParserState::FIND_1A),
      settings(settings_)
{
}

void SocketOutput::start()
{
    std::cerr << "output: start" << std::endl;
    read_command();
}

void SocketOutput::read_command()
{
    auto self(shared_from_this());
    auto buf = std::make_shared< std::vector<std::uint8_t> >(512);    

    async_read(socket, boost::asio::buffer(*buf),
               [this,self,buf] (const boost::system::error_code &ec, std::size_t len) {
                   if (ec) {
                       handle_error(ec);
                   } else {
                       std::cerr << "output: handling command" << std::endl;
                       handle_command(*buf);
                       read_command();
                   }
               });
}

void SocketOutput::handle_command(std::vector<std::uint8_t> data)
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
            handle_option_command(*p);
            state = ParserState::FIND_1A;
            break;
        }
    }
}

void SocketOutput::handle_option_command(uint8_t option)
{
    char ch = (char) option;
    switch (ch) {
    case 'c':
    case 'C':
        settings.binary_format = (ch == 'C');
        break;
    case 'd':
    case 'D':
        settings.filter_df11_df17_only = (ch == 'D');
        break;
    case 'e':
    case 'E':
        settings.mlat_info = (ch == 'E');
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
            settings.mask_df0_df4_df5 = (ch == 'G');
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
        settings.modeac = (ch == 'J');
        break;
    default:
        // unrecognized
        return;
    }

    if (settings_notifier)
        settings_notifier(settings);
}

void SocketOutput::reset_status_message_timer(std::chrono::milliseconds delay)
{
    auto self(shared_from_this());

    status_message_timer.expires_from_now(delay);
    status_message_timer.async_wait([this,self] (const boost::system::error_code &ec) {
            if (!ec) {
                send_synthetic_status_message();
                reset_status_message_timer(std::chrono::seconds(1));
            }
        });
}

typedef std::chrono::duration<std::uint64_t, std::nano> nano_timestamp;
typedef std::chrono::duration<std::uint64_t, std::ratio<1,12000000> > twelvemeg_timestamp;

void SocketOutput::send_synthetic_status_message()
{
    if (!settings.binary_format || last_message_timestamp == 0)
        return;

    std::vector<uint8_t> data;

    data[0] = settings.to_status_byte();
    data[1] = 0;
    data[2] = 0xA0; // new format, emulation active, GPS generally hosed

    // work out a timestamp to use based on the last message we saw +
    // elapsed (system) time since then
    std::uint64_t timestamp;
    auto elapsed = std::chrono::steady_clock::now() - last_message_clock;
    if (settings.gps_timestamps) {
        std::uint64_t last_seconds = last_message_timestamp >> 30;
        std::uint64_t last_nanos = last_message_timestamp & 0x3FFFFFFF;

        std::uint64_t ns_elapsed = std::chrono::duration_cast<nano_timestamp>(elapsed).count();
        std::uint64_t nanos = last_nanos + ns_elapsed % 1000000000;
        if (nanos >= 1000000000) {
            nanos -= 1000000000;
            last_seconds += 1;
        }
        std::uint64_t seconds = last_seconds + (ns_elapsed / 1000000000) % 86400;
        timestamp = (seconds << 30) | nanos;
    } else {
        // this can just overflow whenever, we don't care
        timestamp = last_message_timestamp + std::chrono::duration_cast<twelvemeg_timestamp>(elapsed).count();
    }

    write_binary_message(beastsplitter::message::MessageType::STATUS,
                         timestamp,
                         0,
                         data);
}   

void SocketOutput::dispatch_message(const beastsplitter::message::Message &message)
{
    if (!socket.is_open())
        return; // we are shut down

    // notice when we switch to GPS timestamps
    if (message.type == beastsplitter::message::MessageType::STATUS) {
        Settings s = Settings::from_status_byte(message.data[0]);
        receiving_gps_timestamps = s.gps_timestamps;

        // We should see one status message per second.
        // If not, assume we lost the receiver (or it reconnected)
        // and generate some synthetic ones.
        reset_status_message_timer(std::chrono::seconds(2));

        if (!settings.radarcape || !settings.binary_format) {
            // 0x34 status messages are only sent on binary radarcape connections
            return;
        }
    }

    // Fix up timestamp to match what was requested

    uint64_t timestamp = 0;
    if (settings.binary_format || settings.mlat_info) {
        // Pick a suitable timestamp.
        if (settings.gps_timestamps == receiving_gps_timestamps) {
            // No conversion needed.
            timestamp = message.timestamp;
        } else if (settings.gps_timestamps && !receiving_gps_timestamps) {
            // They want a GPS timestamp, but we don't have one, scale up the 12MHz clock
            uint64_t seconds = (message.timestamp / 12000000) % 86400;
            uint64_t nanos = (message.timestamp % 12000000) * 1000 / 12;
            timestamp = (seconds << 30) | nanos;
        } else /* if (!settings.gps_timestamps && receiving_gps_timestamps) */ {
            // We have a GPS timestamp, but they want a 12MHz clock
            uint64_t seconds = message.timestamp >> 30;
            uint64_t nanos = message.timestamp & 0x3FFFFFFFULL;
            timestamp = nanos * 1000 / 12 + seconds * 12000000;
        }
    }

    last_message_timestamp = timestamp; // nb: after conversion.
    last_message_clock = std::chrono::steady_clock::now();    

    if (message.type == beastsplitter::message::MessageType::STATUS) {
        // put the connection-specific dipswitch settings in
        std::vector<std::uint8_t> copy = message.data;
        copy[0] = settings.to_status_byte();
        write_message(message.type, timestamp, message.signal, copy);
    } else {
        write_message(message.type, timestamp, message.signal, message.data);
    }
}

void SocketOutput::write_message(beastsplitter::message::MessageType type,
                                 std::uint64_t timestamp,
                                 std::uint8_t signal,
                                 const std::vector<std::uint8_t> &data)
{
    std::cerr << "output: writing a message" << std::endl;

    if (settings.binary_format)
        write_binary_message(type, timestamp, signal, data);
    else if (settings.mlat_info)
        write_avrmlat_message(timestamp, data);
    else
        write_avr_message(data);
}

static void push_back_escape(std::vector<std::uint8_t> &v, std::uint8_t b)
{
    if (b == 0x1A)
        v.push_back(0x1A);
    v.push_back(b);
}

void SocketOutput::write_binary_message(beastsplitter::message::MessageType type,
                                        std::uint64_t timestamp,
                                        std::uint8_t signal,
                                        const std::vector<std::uint8_t> &data)
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

    socket_write(msg);
}

// we could use ostrstream here, I guess, but this is simpler

static void push_back_hex(std::vector<char> &v, std::uint8_t b)
{
    static const char *hexdigits = "0123456789ABCDEF";

    v.push_back(hexdigits[(b >> 4) & 0x0F]);
    v.push_back(hexdigits[b & 0x0F]);
}

void SocketOutput::write_avr_message(const std::vector<std::uint8_t> &data)
{
    // build a vector with the data we want to write
    auto msg = std::make_shared< std::vector<char> >();
    msg->reserve(3 + data.size() * 2);
   
    msg->push_back('*');
    for (auto b : data)
        push_back_hex(*msg, b);
    msg->push_back(';');
    msg->push_back('\n');

    socket_write(msg);
}

void SocketOutput::write_avrmlat_message(std::uint64_t timestamp, const std::vector<std::uint8_t> &data)
{
    // build a vector with the data we want to write
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

    socket_write(msg);
}

template <class T>
void SocketOutput::socket_write(std::shared_ptr<T> msg)
{
    auto self(shared_from_this());
    async_write(socket, boost::asio::buffer(*msg),
                [this,self,msg] (const boost::system::error_code &ec, size_t len) {
                    if (ec)
                        handle_error(ec);
                });
}

void SocketOutput::handle_error(const boost::system::error_code &ec)
{
    std::cerr << "output: connection error seen: " << ec.message() << std::endl;
    close();
}

void SocketOutput::close()
{
    socket.close();
    status_message_timer.cancel();
    if (close_notifier)
        close_notifier();
}    


//////////////

SocketListener::SocketListener(boost::asio::io_service &service_,
                               boost::asio::ip::tcp::endpoint &endpoint_,
                               const Settings &settings_)
    : service(service_),
      acceptor(service_, endpoint_),
      socket(service_),
      settings(settings_)
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
                                  SocketOutput::pointer new_output =
                                      SocketOutput::create(service, socket, settings);
                                  connections.push_back(new_output);
                                  new_output->set_settings_notifier(settings_notifier);
                                  new_output->start();
                                  accept_connection();
                              }
                          });
}

void SocketListener::dispatch_message(const beastsplitter::message::Message &message)
{
    if (message.type == beastsplitter::message::MessageType::STATUS) {
        std::cerr << "radarcape status with settings " << std::hex << std::setw(2) << std::setfill('0') << (int)message.data[0] << std::dec << std::endl;
    }

    for (auto wp : connections) {
        if (auto c = wp.lock()) {
            std::cerr << "DISPATCH (connection)" << std::endl;
            c->dispatch_message(message);
        }
    }

    // expire closed connections
    connections.erase(std::remove_if(connections.begin(), connections.end(),
                                     std::mem_fn(&std::weak_ptr<SocketOutput>::expired)),
                      connections.end());
}
