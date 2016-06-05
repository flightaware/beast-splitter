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

#include "beast_input.h"
#include "beast_input_serial.h"
#include "beast_input_net.h"
#include "beast_output.h"
#include "modes_filter.h"
#include "status_writer.h"

#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/program_options.hpp>
#include <boost/regex.hpp>

#include <memory>
#include <iostream>

namespace po = boost::program_options;
using boost::asio::ip::tcp;

struct net_option {
    std::string host;
    std::string port;
};

struct output_option {
    std::string host;
    std::string port;
    beast::Settings settings;
};

struct listen_option : output_option {};
struct connect_option : output_option {};

// Specializations of validate for --listen / --connect / --net
void validate(boost::any& v,
              const std::vector<std::string>& values,
              net_option* target_type, int)
{
    po::validators::check_first_occurrence(v);
    const std::string &s = po::validators::get_single_string(values);

    static const boost::regex r("([^:]+):(\\d+)");
    boost::smatch match;
    if (boost::regex_match(s, match, r)) {
        net_option o;
        o.host = match[1];
        o.port = match[2];
        v = boost::any(o);
    } else {
        throw po::validation_error(po::validation_error::invalid_option_value);
    }
}

void validate(boost::any& v,
              const std::vector<std::string>& values,
              connect_option* target_type, int)
{
    po::validators::check_first_occurrence(v);
    const std::string &s = po::validators::get_single_string(values);

    static const boost::regex r("([^:]+):(\\d+)(?::([a-zA-Z]+))?");
    boost::smatch match;
    if (boost::regex_match(s, match, r)) {
        connect_option o;
        o.host = match[1];
        o.port = match[2];
        o.settings = beast::Settings(match[3]);
        v = boost::any(o);
    } else {
        throw po::validation_error(po::validation_error::invalid_option_value);
    }
}

void validate(boost::any& v,
              const std::vector<std::string>& values,
              listen_option* target_type, int)
{
    po::validators::check_first_occurrence(v);
    const std::string &s = po::validators::get_single_string(values);

    static const boost::regex r("(?:([^:]+):)?(\\d+)(?::([a-zA-Z]+))?");
    boost::smatch match;
    if (boost::regex_match(s, match, r)) {
        listen_option o;
        o.host = match[1];
        o.port = match[2];
        o.settings = beast::Settings(match[3]);
        v = boost::any(o);
    } else {
        throw po::validation_error(po::validation_error::invalid_option_value);
    }
}

namespace beast {
    void validate(boost::any& v,
                  const std::vector<std::string>& values,
                  beast::Settings* target_type, long int)
    {
        po::validators::check_first_occurrence(v);
        const std::string &s = po::validators::get_single_string(values);

        static const boost::regex r("[cdefghijbrCDEFGHIJBR]*");
        if (boost::regex_match(s, r)) {
            v = boost::any(beast::Settings(s));
        } else {
            throw po::validation_error(po::validation_error::invalid_option_value);
        }
    }
}

static int realmain(int argc, char **argv)
{
    boost::asio::io_service io_service;
    modes::FilterDistributor distributor;

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("serial", po::value<std::string>(), "read from given serial device")
        ("net", po::value<net_option>(), "read from given network host:port")
        ("status-file", po::value<std::string>(), "set path to status file")
        ("fixed-baud", po::value<unsigned>()->default_value(0), "set a fixed baud rate, or 0 for autobauding")
        ("listen", po::value< std::vector<listen_option> >(), "specify a [host:]port[:settings] to listen on")
        ("connect", po::value< std::vector<connect_option> >(), "specify a host:port[:settings] to connect to")
        ("force", po::value<beast::Settings>()->default_value(beast::Settings()), "specify settings to force on or off when configuring the Beast");

    po::variables_map opts;

    try {
        po::store(po::parse_command_line(argc, argv, desc), opts);
        po::notify(opts);
    } catch (boost::program_options::error &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << desc << std::endl;
        return 1;
    }

    if (opts.count("help")) {
        std::cerr << desc << std::endl;
        return 0;
    }

    if (!opts.count("connect") && !opts.count("listen")) {
        std::cerr << "At least one --connect or --listen argument is needed" << std::endl;
        std::cerr << desc << std::endl;
        return 1;
    }

    beast::BeastInput::pointer input;
    if (opts.count("serial")) {
        input = beast::SerialInput::create(io_service,
                                           opts["serial"].as<std::string>(),
                                           opts["fixed-baud"].as<unsigned>(),
                                           opts["force"].as<beast::Settings>());
    } else if (opts.count("net")) {
        auto net = opts["net"].as<net_option>();
        input = beast::NetInput::create(io_service,
                                        net.host,
                                        net.port,
                                        opts["force"].as<beast::Settings>());
    } else {
        std::cerr << "A --serial or --net argument is needed" << std::endl;
        std::cerr << desc << std::endl;
        return 1;
    }

    distributor.set_filter_notifier(std::bind(&beast::BeastInput::set_filter, input, std::placeholders::_1));

    tcp::resolver resolver(io_service);

    if (opts.count("listen")) {
        for (auto l : opts["listen"].as< std::vector<listen_option> >()) {
            tcp::resolver::query query(l.host, l.port, tcp::resolver::query::passive);
            boost::system::error_code ec;

            bool success = false;
            tcp::resolver::iterator end;
            for (auto i = resolver.resolve(query, ec); i != end; ++i) {
                const auto &endpoint = i->endpoint();

                try {
                    auto listener = beast::SocketListener::create(io_service, endpoint, distributor, l.settings);
                    listener->start();
                    std::cerr << "Listening on " << endpoint << std::endl;
                    success = true;
                } catch (boost::system::system_error &err) {
                    std::cerr << "Could not listen on " << endpoint << ": " << err.what() << std::endl;
                    ec = err.code();
                }
            }

            if (!success) {
                if (l.host.empty())
                    std::cerr << "Could not bind to port " << l.port << ": " << ec.message() << std::endl;
                else
                    std::cerr << "Could not bind to " << l.host << ":" << l.port << ": " << ec.message() << std::endl;
                return 1;
            }
        }
    }

    if (opts.count("connect")) {
        for (auto l : opts["connect"].as< std::vector<connect_option> >()) {
            auto connector = beast::SocketConnector::create(io_service, l.host, l.port, distributor, l.settings);
            connector->start();
        }
    }

    if (opts.count("status-file")) {
        auto statuswriter = splitter::StatusWriter::create(io_service, distributor, input, opts["status-file"].as<std::string>());
        statuswriter->start();
    }

    input->set_message_notifier(std::bind(&modes::FilterDistributor::broadcast, &distributor, std::placeholders::_1));
    input->start();

    io_service.run();
    return 0;
}

int main(int argc, char **argv)
{
    try {
        return realmain(argc, argv);
    } catch (std::exception &e) {
        std::cerr << "Uncaught exception: " << e.what() << std::endl;
        return 99;
    }
}
