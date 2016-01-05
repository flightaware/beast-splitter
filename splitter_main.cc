#include "beast_input.h"
#include "beast_output.h"
#include "modes_filter.h"

#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/program_options.hpp>
#include <boost/regex.hpp>

#include <memory>
#include <iostream>

namespace po = boost::program_options;
using boost::asio::ip::tcp;

struct output_option {
    std::string host;
    std::string port;
    beast::Settings settings;
};

struct listen_option : output_option {};
struct connect_option : output_option {};

// Specializations of validate for --listen / --connect
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

int main(int argc, char **argv)
{
    boost::asio::io_service io_service;
    modes::FilterDistributor distributor;

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("serial", po::value<std::string>()->default_value("/dev/beast"), "set path to serial device")
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

    auto serial = beast::SerialInput::create(io_service,
                                             opts["serial"].as<std::string>(),
                                             opts["fixed-baud"].as<unsigned>(),
                                             opts["force"].as<beast::Settings>());
    distributor.set_filter_notifier(std::bind(&beast::SerialInput::set_filter, serial, std::placeholders::_1));

    tcp::resolver resolver(io_service);

    if (opts.count("listen")) {
        for (auto l : opts["listen"].as< std::vector<listen_option> >()) {
            tcp::resolver::query query(l.host, l.port, tcp::resolver::query::passive | tcp::resolver::query::address_configured);
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
                std::cerr << "Could not bind to any addresses of " << l.host << ": " << ec.message() << std::endl;
                return 2;
            }
        }
    }

    serial->set_message_notifier(std::bind(&modes::FilterDistributor::broadcast, &distributor, std::placeholders::_1));
    serial->start();

    io_service.run();
    return 0;
}
