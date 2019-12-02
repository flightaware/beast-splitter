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

#include "beast_settings.h"

#include <cctype>

namespace beast {
    Settings::Settings() {}

    Settings::Settings(std::uint8_t b)
        : radarcape(true),
          binary_format((b & 0x01) != 0),
          filter_11_17_18((b & 0x02) != 0),
          avrmlat((b & 0x04) != 0),
          crc_disable((b & 0x08) != 0),
          gps_timestamps((b & 0x10) != 0),
          rts_handshake((b & 0x20) != 0),
          fec_disable((b & 0x40) != 0),
          modeac_enable((b & 0x80) != 0)
    {}

    Settings::Settings(const modes::Filter &filter)
        : filter_11_17_18(true),
          crc_disable(filter.receive_bad_crc),
          gps_timestamps(filter.receive_gps_timestamps),
          fec_disable(!filter.receive_fec),
          modeac_enable(filter.receive_modeac),
          filter_0_4_5(!filter.receive_df[0] && !filter.receive_df[4] && filter.receive_df[5])
    {
        for (auto i = 0; i < 32; ++i) {
            if (filter.receive_df[i] && i != 11 && i != 17 && i != 18) {
                filter_11_17_18 = false;
                break;
            }
        }

        // only enable verbatim if someone downstream requested it; otherwise
        // leave it as DONTCARE to avoid generating 'v'-setting messages if we
        // can avoid it
        if (filter.receive_verbatim)
            verbatim = true;
    }

    Settings::Settings(const std::string &str)
    {
        // starts with everything dontcare

        for (char ch : str) {
            switch (ch) {
            case 'B': radarcape = false; break;    // no equivalent dipswitch
            case 'R': radarcape = true; break;     // no equivalent dipswitch
            case 'c': binary_format = false; break;
            case 'C': binary_format = true; break;
            case 'd': filter_11_17_18 = false; break;
            case 'D': filter_11_17_18 = true; break;
            case 'e': avrmlat = false; break;
            case 'E': avrmlat = true; break;
            case 'f': crc_disable = false; break;
            case 'F': crc_disable = true; break;
            case 'g': gps_timestamps = false; break;
            case 'G': gps_timestamps = true; break;
            case 'h': rts_handshake = false; break;
            case 'H': rts_handshake = true; break;
            case 'i': fec_disable = false; break;
            case 'I': fec_disable = true; break;
            case 'j': modeac_enable = false; break;
            case 'J': modeac_enable = true; break;
            case 'k': filter_0_4_5 = false; break; // this is g/G on the Beast, but we separate it out
            case 'K': filter_0_4_5 = true; break;
            case 'v': verbatim = false; break;
            case 'V': verbatim = true; break;
            }
        }

        // ensure settings are selfconsistent
        if (radarcape.off() && !gps_timestamps.dontcare())
            gps_timestamps = false;
        else if (radarcape.on() && !filter_0_4_5.dontcare())
            filter_0_4_5 = false;
    }

    Settings Settings::operator|(const Settings &other) const
    {
        Settings s = *this;
        s.binary_format |= other.binary_format;
        s.filter_11_17_18 |= other.filter_11_17_18;
        s.avrmlat |= other.avrmlat;
        s.crc_disable |= other.crc_disable;
        s.gps_timestamps |= other.gps_timestamps;
        s.rts_handshake |= other.rts_handshake;
        s.fec_disable |= other.fec_disable;
        s.modeac_enable |= other.modeac_enable;
        s.filter_0_4_5 |= other.filter_0_4_5;
        s.radarcape |= other.radarcape;
        s.verbatim |= other.verbatim;
        return s;
    }

    std::uint8_t Settings::to_status_byte() const
    {
        if (!radarcape)
            return 0;   // only the radarcape has the status reporting

        return
            (binary_format ? 0x01 : 0) |
            (filter_11_17_18 ? 0x02 : 0) |
            (avrmlat ? 0x04 : 0) |
            (crc_disable ? 0x08 : 0) |
            (gps_timestamps ? 0x10 : 0) |
            (rts_handshake ? 0x20 : 0) |
            (fec_disable ? 0x40 : 0) |
            (modeac_enable ? 0x80 : 0);
    }

    modes::Filter Settings::to_filter() const
    {
        modes::Filter f;

        if (filter_11_17_18) {
            f.receive_df.fill(false);
            f.receive_df[11] = true;
            f.receive_df[17] = true;
            f.receive_df[18] = true;
        } else {
            f.receive_df.fill(true);
            if (filter_0_4_5) {
                f.receive_df[0] = false;
                f.receive_df[4] = false;
                f.receive_df[5] = false;
            }
        }

        f.receive_modeac = modeac_enable;
        f.receive_bad_crc = crc_disable;
        f.receive_fec = !fec_disable;
        f.receive_status = !radarcape.off();
        f.receive_gps_timestamps = !radarcape.off() && !gps_timestamps.off();
        f.receive_position = position_enable;
        f.receive_verbatim = verbatim;

        return f;
    }

    static void add_setting(helpers::bytebuf &msg, bool onoff, char off, char on)
    {
        msg.push_back(0x1a);
        msg.push_back((std::uint8_t) '1');
        msg.push_back((std::uint8_t) (onoff ? on : off));
    }

    template <bool D,char OFF,char ON>
    static void add_setting(helpers::bytebuf &msg, const Settings::tristate<D,OFF,ON> &onoff)
    {
        if (!onoff.dontcare())
            add_setting(msg, (bool)onoff, OFF, ON);
    }

    helpers::bytebuf Settings::to_message() const
    {
        helpers::bytebuf msg;

        if (radarcape.dontcare())
            throw std::logic_error("need to explictly select radarcape or beast when generating settings messages");

        add_setting(msg, binary_format);
        add_setting(msg, filter_11_17_18);
        add_setting(msg, avrmlat);
        add_setting(msg, crc_disable);
        // this is a little special because of the ambiguity between radarcape and beast
        if (!radarcape && !filter_0_4_5.dontcare())
            add_setting(msg, filter_0_4_5, 'g', 'G');
        else if (radarcape && !gps_timestamps.dontcare())
            add_setting(msg, gps_timestamps, 'g', 'G');
        add_setting(msg, rts_handshake);
        add_setting(msg, fec_disable);
        add_setting(msg, modeac_enable);
        add_setting(msg, verbatim);

        return msg;
    }

    Settings Settings::apply_defaults() const
    {
        Settings s;
        s.binary_format = (bool)binary_format;
        s.filter_11_17_18 = (bool)filter_11_17_18;
        s.avrmlat = (bool)avrmlat;
        s.crc_disable = (bool)crc_disable;
        s.gps_timestamps = (bool)gps_timestamps;
        s.rts_handshake = (bool)rts_handshake;
        s.fec_disable = (bool)fec_disable;
        s.modeac_enable = (bool)modeac_enable;
        s.radarcape = (bool)radarcape;
        s.filter_0_4_5 = (bool)filter_0_4_5;
        s.verbatim = (bool)verbatim;
        return s;
    }

    std::ostream &operator<<(std::ostream &os, const Settings &s)
    {
        return (os << s.radarcape << s.binary_format
                << s.filter_11_17_18 << s.avrmlat
                << s.crc_disable << s.gps_timestamps
                << s.rts_handshake << s.fec_disable
                << s.modeac_enable << s.filter_0_4_5
                << s.verbatim);
    }
};
