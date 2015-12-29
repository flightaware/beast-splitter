#include "beast_input.h"
#include "beast_output.h"
#include "beast_message.h"

#include <memory>
#include <iostream>

using namespace std;
using namespace beastsplitter::input;
using namespace beastsplitter::output;
using namespace beastsplitter::message;

void handle_a_message(const Message &message)
{
    cerr << ".";
}

int main(int argc, char **argv)
{
    boost::asio::io_service io_service;

    auto serial = SerialInput::create(io_service,
                                      "/dev/beastgps0");

    boost::asio::ip::tcp::endpoint listen_addr(boost::asio::ip::address_v4::any(),
                                               12345);

    beastsplitter::output::Settings settings;
    settings.binary_format = false;
    settings.mlat_info = false;
    auto listener = SocketListener::create(io_service, listen_addr, settings);

    serial->set_message_notifier([&] (const Message &message) {
            listener->dispatch_message(message);
        });

    listener->start();
    serial->start();
 
    io_service.run();
    return 0;
}
