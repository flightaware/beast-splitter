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

#ifndef MODES_FILTER_H
#define MODES_FILTER_H

#include <array>
#include <ostream>

#include "modes_message.h"

namespace modes {
    struct Filter {
        std::array<bool,32> receive_df;
        bool receive_modeac;
        bool receive_bad_crc;
        bool receive_fec;
        bool receive_status;
        bool receive_gps_timestamps;
        bool receive_position;

        Filter();

        void inplace_combine(const Filter &two);
        static Filter combine(const Filter &one, const Filter &two);

        bool operator==(const Filter &other) const;
        bool operator!=(const Filter &other) const;

        bool operator()(const Message &message) const {
            switch (message.type()) {
            case MessageType::MODE_AC:
                return receive_modeac;
            case MessageType::STATUS:
                return receive_status;
            case MessageType::MODE_S_SHORT:
            case MessageType::MODE_S_LONG:
                if (!receive_df[message.df()])
                    return false;
                if (message.crc_bad() && !receive_bad_crc)
                    return false;
                return true;
            case MessageType::POSITION:
                return receive_position;

            default:
                // what is this?
                return false;
            }
        }
    };

    std::ostream &operator<<(std::ostream &os, const Filter &f);

    class FilterDistributor {
    public:
        typedef unsigned int handle;
        typedef std::function<void(const Filter&)> FilterNotifier;
        typedef std::function<void(const Message&)> MessageNotifier;

        FilterDistributor();
        FilterDistributor(const FilterDistributor& that) = delete;
        FilterDistributor &operator=(const FilterDistributor& that) = delete;

        void set_filter_notifier(FilterNotifier f);

        handle add_client(MessageNotifier message_notifier, const Filter &initial_filter);
        void update_client_filter(handle client, const Filter &new_filter);
        void remove_client(handle client);

        void broadcast(const Message& message);

    private:
        void update_upstream_filter();

        handle next_handle;
        FilterNotifier filter_notifier;

        struct client {
            MessageNotifier notifier;
            Filter filter;
            bool deleted;
        };

        std::map<handle, client> clients;
    };
};

#endif
