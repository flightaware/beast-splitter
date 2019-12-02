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

#include "crc.h"

#include <boost/preprocessor/repetition/enum.hpp>

namespace crc::detail {
    // generates the CRC table at compile time (!)
    const std::uint32_t crc_polynomial = 0xfff409U;

    template <std::uint32_t c, int k = 8>
    struct crcgen : crcgen<((c & 0x00800000) ? crc_polynomial : 0) ^ (c << 1), k - 1> {};

    template <std::uint32_t c>
    struct crcgen<c, 0>
    {
        enum { value = (c & 0x00FFFFFF) };
    };

#define CRCGEN(Z, N, _) detail::crcgen<N << 16>::value
    std::uint32_t crc_table[256] = { BOOST_PP_ENUM(256, CRCGEN, _) };
#undef CRCGEN

    std::map<std::uint32_t,unsigned> syndromes;

    void init_syndromes() {
        syndromes.clear();

        std::vector<std::uint8_t> message(112 / 8);
        for (unsigned i = 5; i < 112; ++i) {
            message[i/8] ^= 1 << (7 - (i & 7));
            syndromes[message_residual(message)] = i;
            message[i/8] ^= 1 << (7 - (i & 7));
        }
    }
}; // namespace crc::detail
