#include "beast_input.h"
#include "beast_output.h"
#include "modes_filter.h"

#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <memory>
#include <iostream>

int main(int argc, char **argv)
{
    boost::asio::io_service io_service;
    modes::FilterDistributor distributor;

    auto serial = beast::SerialInput::create(io_service,
                                             "/dev/beastgps0");
    distributor.set_filter_notifier(std::bind(&beast::SerialInput::set_filter, serial, std::placeholders::_1));

    boost::asio::ip::tcp::endpoint listen_addr(boost::asio::ip::address_v4::any(),
                                               12345);

    // defaults:
    beast::Settings settings;
    settings.radarcape = true;
    settings.gps_timestamps = true;
    auto listener = beast::SocketListener::create(io_service, listen_addr, distributor, settings);

    serial->set_message_notifier(std::bind(&modes::FilterDistributor::broadcast, &distributor, std::placeholders::_1));

    listener->start();
    serial->start(); 
    io_service.run();
    return 0;
}
