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
          filter_0_4_5(!filter.receive_df[0] && !filter.receive_df[4] && filter.receive_df[5]),
          gps_timestamps(filter.receive_gps_timestamps),
          fec_disable(!filter.receive_fec),
          modeac_enable(filter.receive_modeac)          
    {
        for (auto i = 0; i < 32; ++i) {
            if (filter.receive_df[i] && i != 11 && i != 17 && i != 18) {
                filter_11_17_18 = false;
                break;
            }
        }
    }   

    Settings::Settings(const std::string &str)
    {
        // starts with everything dontcare

        for (char ch : str) {
            switch (ch) {
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
            case 'b': filter_0_4_5 = false; break; // this is g/G on the Beast, but we separate it out
            case 'B': filter_0_4_5 = true; break;
            case 'r': radarcape = false; break;    // no equivalent dipswitch
            case 'R': radarcape = true; break;
            }
        }
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
        f.receive_gps_timestamps = gps_timestamps;

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
        add_setting(msg, (bool)onoff, OFF, ON);
    }

    helpers::bytebuf Settings::to_message() const
    {
        helpers::bytebuf msg;

        add_setting(msg, binary_format);
        add_setting(msg, filter_11_17_18);
        add_setting(msg, avrmlat);
        add_setting(msg, crc_disable);
        // this is a little special because of the ambiguity between radarcape and beast
        add_setting(msg, radarcape ? gps_timestamps : filter_0_4_5, 'g', 'G');
        add_setting(msg, rts_handshake);
        add_setting(msg, fec_disable);
        add_setting(msg, modeac_enable);

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
        return s;
    }

    std::ostream &operator<<(std::ostream &os, const Settings &s)
    {
        return (os << s.binary_format << s.filter_11_17_18
                << s.avrmlat << s.crc_disable
                << s.gps_timestamps << s.rts_handshake
                << s.fec_disable << s.modeac_enable
                << s.radarcape << s.filter_0_4_5);
    }
};
