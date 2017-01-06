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

#ifndef BEAST_SETTINGS_H
#define BEAST_SETTINGS_H

#include <cstdint>

#include "helpers.h"
#include "modes_filter.h"

namespace beast {
    // Beast dipswitch settings that can be software-controlled
    // There is a synthetic bit to say whether these are
    // Beast-classic settings (no GPS timestamps, DF0/4/5 filter available)
    // or Radarcape settings (GPS timestamps available, no DF0/4/5 filter)
    struct Settings {
        // a setting that can be explicitly ON, explicitly OFF, or default DONTCARE
        // DONTCARE means either on or off based on the D template parameter
        template <bool D, char OFF, char ON>
        class tristate {
        public:
            tristate() : state(0) {}
            tristate(bool b) : state(b ? 1 : -1) {}

            tristate &operator=(bool b) {
                state = (b ? 1 : -1);
                return *this;
            }

            operator bool() const {
                return (dontcare() ? D : on());
            }

            bool operator!() const {
                return !(dontcare() ? D : on());
            }

            bool on() const {
                return (state > 0);
            }

            bool off() const {
                return (state < 0);
            }

            bool dontcare() const {
                return (state == 0);
            }

            // operator+ combines two settings with equal weight
            // given to both.
            //
            // DONTCARE + X == X + DONTCARE == X
            // ON + ON  == ON
            // ON + OFF == OFF + ON == DONTCARE
            // OFF + OFF == OFF
            tristate<D,OFF,ON> operator+(const tristate<D,OFF,ON> &other) const {
                if (state == 0)
                    return other;
                else if (other.state == 0)
                    return *this;
                else if (state == other.state)
                    return *this;
                else
                    return tristate<D,OFF,ON>(); // DONTCARE
            }

            tristate<D,OFF,ON> &operator+=(const tristate<D,OFF,ON> &other) {
                if (state == 0)
                    state = other.state;
                else if (other.state != 0 && state != other.state)
                    state = 0;
                return *this;
            }

            // operator| uses the lefthand side in preference to the righthand side
            // DONTCARE | X == X
            // ON | X == ON
            // OFF | X == OFF
            tristate<D,OFF,ON> operator|(const tristate<D,OFF,ON> &other) const {
                if (state == 0)
                    return other;
                else
                    return *this;
            }

            tristate<D,OFF,ON> &operator|=(const tristate<D,OFF,ON> &other) {
                if (state == 0)
                    state = other.state;
                return *this;
            }

        private:
            int state;
        };

        // default ctor sets all to dontcare
        Settings();

        // ctor from a reported settings byte
        // as only the radarcape reports settings, this
        // is assumed to be a radarcape
        Settings(std::uint8_t b);

        // ctor to satisfy a given filter
        // sets non-filtering things to dontcare
        Settings(const modes::Filter &filter);

        // set from a string (of the format cdeFGhIj where caps are on, lower are off, missing are dontcare)
        Settings(const std::string &str);

        // convert the settings to a status byte
        std::uint8_t to_status_byte() const;

        // convert the settings to a Filter
        modes::Filter to_filter(void) const;

        // convert the settings to a message suitable for sending to the Beast/Radarcape
        // to set those settings
        helpers::bytebuf to_message() const;

        Settings apply_defaults() const;

        Settings operator|(const Settings &other) const;

        tristate<false, 'B', 'R'> radarcape;        // (B)east vs (R)adarcape
        tristate<true,  'c', 'C'> binary_format;    // off=AVR, on=binary
        tristate<false, 'd', 'D'> filter_11_17_18;  // off=no filter, on=send only DF11/17/18
        tristate<true,  'e', 'E'> avrmlat;          // off=no timestamps in AVR, on=include timestamps in AVR
        tristate<false, 'f', 'F'> crc_disable;      // off=normal CRC checks, on=no CRC checks
        tristate<true,  'g', 'G'> gps_timestamps;   // off=12MHz timestamps, on=GPS timestamps (Radarcape only)
        tristate<true,  'h', 'H'> rts_handshake;    // off=no flow control, on=RTS/CTS flow control
        tristate<false, 'i', 'I'> fec_disable;      // off=1-bit FEC enabled, on=no FEC
        tristate<false, 'j', 'J'> modeac_enable;    // off=no Mode A/C, on=send Mode A/C
        tristate<false, 'k', 'K'> filter_0_4_5;     // off=no filter, on=don't send DF0/4/5 (Beast only)
        tristate<false, 'p', 'P'> position_enable;  // off=don't send position messages, on=send position message (Radarcape only, not a real setting)
    };

    template <bool D,char OFF,char ON>
    std::ostream &operator<<(std::ostream &os, const Settings::tristate<D,OFF,ON> &s) {
        if (s.on())
            return (os << ON);
        else if (s.off())
            return (os << OFF);
        else
            return os;
    }

    std::ostream &operator<<(std::ostream &os, const Settings &s);
};

#endif
