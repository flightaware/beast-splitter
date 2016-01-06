#include <iostream>
#include <iomanip>

#include "modes_message.h"

#include <boost/preprocessor/repetition/enum.hpp>

namespace modes {
    std::ostream& operator<<(std::ostream &os, const Message &message) {
        os << std::hex << std::setfill('0')
           << message.type() << "@"
           << std::setw(12) << message.timestamp()
           << ":";
        for (auto b : message.data()) {
            os << std::setw(2) << (int)b;
        }
        os << std::dec << std::setfill(' ');
        return os;
    }

    // generates the CRC table at compile time (!)
    namespace detail {
        const std::uint32_t crc_polynomial = 0xfff409U;

        template <std::uint32_t c, int k = 8>
        struct crcgen : crcgen<((c & 0x00800000) ? crc_polynomial : 0) ^ (c << 1), k - 1> {};

        template <std::uint32_t c>
        struct crcgen<c, 0>
        {
            enum { value = (c & 0x00FFFFFF) };
        };
    };

#define CRCGEN(Z, N, _) detail::crcgen<N << 16>::value
    std::uint32_t crc_table[256] = { BOOST_PP_ENUM(256, CRCGEN, _) };
#undef CRCGEN
};
