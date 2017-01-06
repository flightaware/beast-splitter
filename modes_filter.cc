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

#include "modes_filter.h"

#include <iostream>

namespace modes {
    Filter::Filter()
        : receive_modeac(false),
          receive_bad_crc(false),
          receive_fec(false),
          receive_status(false),
          receive_gps_timestamps(false),
          receive_position(false)
    {
        receive_df.fill(false);
    }

    void Filter::inplace_combine(const Filter &two) {
        for (std::size_t i = 0; i < receive_df.size(); ++i)
            receive_df[i] = receive_df[i] || two.receive_df[i];

        receive_modeac = receive_modeac || two.receive_modeac;
        receive_bad_crc = receive_bad_crc || two.receive_bad_crc;
        receive_fec = receive_fec || two.receive_fec;
        receive_status = receive_status || two.receive_status;
        receive_gps_timestamps = receive_gps_timestamps || two.receive_gps_timestamps;
        receive_position = receive_position || two.receive_position;
    }

    Filter Filter::combine(const Filter &one, const Filter &two)
    {
        Filter newFilter = one;
        newFilter.inplace_combine(two);
        return newFilter;
    }

    bool Filter::operator==(const Filter &other) const
    {
        if (this == &other)
            return true;

        return (receive_modeac == other.receive_modeac &&
                receive_bad_crc == other.receive_bad_crc &&
                receive_fec == other.receive_fec &&
                receive_status == other.receive_status &&
                receive_gps_timestamps == other.receive_gps_timestamps &&
                receive_position == other.receive_position &&
                receive_df == other.receive_df);
    }

    bool Filter::operator!=(const Filter &other) const
    {
        return !(*this == other);
    }

    std::ostream &operator<<(std::ostream &os, const Filter &f)
    {
        os << "Filter[ ";
        if (f.receive_modeac)
            os << "modeac ";
        if (f.receive_bad_crc)
            os << "badcrc ";
        if (f.receive_fec)
            os << "fec ";
        if (f.receive_status)
            os << "status ";
        if (f.receive_gps_timestamps)
            os << "gps ";
        if (f.receive_position)
            os << "position ";
        for (std::size_t i = 0; i < f.receive_df.size(); ++i)
            if (f.receive_df[i])
                os << i << " ";
        os << "]";
        return os;
    }

    FilterDistributor::FilterDistributor()
        : next_handle(0)
    {
    }

    void FilterDistributor::set_filter_notifier(FilterNotifier f)
    {
        filter_notifier = f;
    }

    FilterDistributor::handle FilterDistributor::add_client(MessageNotifier message_notifier,
                                                            const Filter &initial_filter)
    {
        handle h = next_handle++;
        clients[h] = {
            message_notifier,
            initial_filter,
            false
        };
        update_upstream_filter();
        return h;
    }

    void FilterDistributor::update_client_filter(handle h,
                                                 const Filter &new_filter)
    {
        auto i = clients.find(h);
        if (i == clients.end())
            return;

        client &c = i->second;
        if (c.deleted)
            return;

        if (c.filter == new_filter)
            return;

        c.filter = new_filter;
        update_upstream_filter();
    }

    void FilterDistributor::remove_client(handle h)
    {
        auto i = clients.find(h);
        if (i == clients.end())
            return;

        client &c = i->second;
        if (c.deleted)
            return;

        c.deleted = true;
        update_upstream_filter();
    }

    void FilterDistributor::broadcast(const Message &message)
    {
        for (auto i = clients.begin(); i != clients.end(); ) {
            client &c = i->second;
            if (!c.deleted && c.filter(message))
                c.notifier(message);

            if (c.deleted)
                clients.erase(i++);
            else
                ++i;
        }
    }

    void FilterDistributor::update_upstream_filter()
    {
        if (!filter_notifier)
            return;

        Filter f;
        for (auto i : clients) {
            client &c = i.second;
            if (c.deleted)
                continue;
            f.inplace_combine(c.filter);
        }

        filter_notifier(f);
    }
};
