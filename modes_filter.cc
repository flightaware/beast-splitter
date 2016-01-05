#include "modes_filter.h"

#include <iostream>

namespace modes {
    Filter::Filter()
        : receive_modeac(false),
          receive_bad_crc(false),
          receive_fec(false),
          receive_status(false),
          receive_gps_timestamps(false)
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
        if (f.receive_gps_timestamps)
            os << "gps ";
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
