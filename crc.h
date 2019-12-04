// -*- c++ -*-

// Copyright (c) 2015-2019, FlightAware LLC.
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

#ifndef CRC_H
#define CRC_H

#include <cstdint>
#include <map>
#include <vector>

namespace crc {
    namespace detail {
        extern std::uint32_t crc_table[256];
        extern std::map<std::uint32_t, unsigned> syndromes_short;
        extern std::map<std::uint32_t, unsigned> syndromes_long;
        void init_syndromes();
    }; // namespace detail

    // Compute the Mode S CRC across the given iterator range
    template <class InputIterator> std::uint32_t crc(InputIterator first, InputIterator last) {
        std::uint32_t c = 0;
        for (InputIterator i = first; i != last; ++i) {
            c = (c << 8) ^ detail::crc_table[*i ^ ((c & 0xff0000) >> 16)];
        }
        return c & 0x00FFFFFF;
    }

    // Compute the Mode S CRC residual for a single Mode S message
    inline std::uint32_t message_residual(const std::vector<std::uint8_t> message) {
        std::size_t len = message.size();
        if (len <= 3) {
            return 0;
        } else {
            auto residual = crc(message.begin(), message.end() - 3);
            residual ^= (message[len - 3] << 16);
            residual ^= (message[len - 2] << 8);
            residual ^= (message[len - 1]);
            return residual;
        }
    }

    // Interpret a CRC residual as a syndrome and for syndromes that correspond
    // to a single bit error, return the affected bit position
    //
    // If the syndrome is not correctable, return -1.

    inline int correctable_bit_short(std::uint32_t syndrome) {
        if (detail::syndromes_short.empty())
            detail::init_syndromes();
        auto correction = detail::syndromes_short.find(syndrome);
        return (correction != detail::syndromes_short.end()) ? correction->second : -1;
    }

    inline int correctable_bit_long(std::uint32_t syndrome) {
        if (detail::syndromes_long.empty())
            detail::init_syndromes();
        auto correction = detail::syndromes_long.find(syndrome);
        return (correction != detail::syndromes_long.end()) ? correction->second : -1;
    }

}; // namespace crc

#endif
