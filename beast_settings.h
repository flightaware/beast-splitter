// -*- c++ -*-

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

        tristate<false, 'r', 'R'> radarcape;        // true if these are radarcape settings, false for beast-classic
        tristate<true,  'c', 'C'> binary_format;    // off=AVR, on=binary
        tristate<false, 'd', 'D'> filter_11_17_18;  // off=no filter, on=send only DF11/17/18
        tristate<true,  'e', 'E'> avrmlat;          // off=no timestamps in AVR, on=include timestamps in AVR
        tristate<false, 'f', 'F'> crc_disable;      // off=normal CRC checks, on=no CRC checks
        tristate<false, 'b', 'B'> filter_0_4_5;     // off=no filter, on=don't send DF0/4/5 (Beast only)
        tristate<true,  'g', 'G'> gps_timestamps;   // off=12MHz timestamps, on=GPS timestamps (Radarcape only)
        tristate<true,  'h', 'H'> rts_handshake;    // off=no flow control, on=RTS/CTS flow control
        tristate<false, 'i', 'I'> fec_disable;      // off=1-bit FEC enabled, on=no FEC
        tristate<false, 'j', 'J'> modeac_enable;    // off=no Mode A/C, on=send Mode A/C
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
