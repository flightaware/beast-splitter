// -*- c++ -*-

#ifndef STATUS_WRITER_H
#define STATUS_WRITER_H

#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>

#include "modes_message.h"
#include "modes_filter.h"
#include "beast_input.h"

namespace splitter {
    class StatusWriter : public std::enable_shared_from_this<StatusWriter> {
    public:
        typedef std::shared_ptr<StatusWriter> pointer;

        const std::chrono::milliseconds timeout_interval = std::chrono::milliseconds(2500);

        // factory method, this class must always be constructed via make_shared
        static pointer create(boost::asio::io_service &service,
                              modes::FilterDistributor &distributor,
                              beast::SerialInput::pointer input,
                              const std::string &path)
        {
            return pointer(new StatusWriter(service, distributor, input, path));
        }

        void start();
        void close();

    private:
        StatusWriter(boost::asio::io_service &service_,
                     modes::FilterDistributor &distributor_,
                     beast::SerialInput::pointer input_,
                     const std::string &path);

        void write(const modes::Message &message);
        void reset_timeout();
        void status_timeout(const boost::system::error_code &ec = boost::system::error_code());
        void write_status_file(const std::string &gps_color = std::string(),
                               const std::string &gps_message = std::string());

        boost::asio::io_service &service;
        modes::FilterDistributor &distributor;
        beast::SerialInput::pointer input;
        std::string path;

        std::string temppath;
        modes::FilterDistributor::handle filter_handle;
        boost::asio::steady_timer timeout_timer;
    };
};

#endif
