// -*- c++ -*-

#ifndef MODES_MESSAGE_H
#define MODES_MESSAGE_H

#include <cstdint>
#include <cassert>
#include <vector>
#include <ostream>
#include <functional>
#include <map>

namespace modes {
    // the type of one message
    enum class MessageType {
        INVALID,
        MODE_AC,
        MODE_S_SHORT,
        MODE_S_LONG,
        STATUS
    };

    enum class TimestampType { UNKNOWN, TWELVEMEG, GPS };

    inline std::ostream& operator<<(std::ostream &os, const MessageType &t) {
        switch (t) {
        case MessageType::MODE_AC: return (os << "MODE_AC");
        case MessageType::MODE_S_SHORT: return (os << "MODE_S_SHORT");
        case MessageType::MODE_S_LONG: return (os << "MODE_S_LONG");
        case MessageType::STATUS: return (os << "STATUS");
        default: return (os << "INVALID");
        }
    }

    inline std::size_t message_size(MessageType type)
    {
        // return the expected number of data bytes for a message of the given type

        switch (type) {
        case MessageType::MODE_AC: return 2;
        case MessageType::MODE_S_SHORT: return 7;
        case MessageType::MODE_S_LONG: return 14;
        case MessageType::STATUS: return 14;
        default: return 0;
        }
    }

    extern std::uint32_t crc_table[256];

    template <class InputIterator>
    std::uint32_t crc(InputIterator first, InputIterator last) {
        std::uint32_t c = 0;
        for (InputIterator i = first; i != last; ++i) {
            c = (c << 8) ^ crc_table[*i ^ ((c & 0xff0000) >> 16)];
        }
        return c & 0x00FFFFFF;
    }

    // a single message
    class Message {
    public:
        Message()
            : m_type(MessageType::INVALID),
              m_timestamp_type(TimestampType::UNKNOWN),
              m_timestamp(0),
              m_signal(0),
              residual(0xFFFFFFFF)
        {}

        Message(MessageType type_,
                TimestampType timestamp_type_, std::uint64_t timestamp_,
                std::uint8_t signal_, std::vector<std::uint8_t> &&data_)
            : m_type(type_),
              m_timestamp_type(timestamp_type_),
              m_timestamp(timestamp_),
              m_signal(signal_),
              m_data(std::move(data_)),
              residual(0xFFFFFFFF)
        {
            assert (m_data.size() == message_size(m_type));
        }

        Message(MessageType type_,
                TimestampType timestamp_type_, std::uint64_t timestamp_,
                std::uint8_t signal_, const std::vector<std::uint8_t> &data_)
            : m_type(type_),
              m_timestamp_type(timestamp_type_),
              m_timestamp(timestamp_),
              m_signal(signal_),
              m_data(data_),
              residual(0xFFFFFFFF)
        {
            assert (m_data.size() == message_size(m_type));
        }

        MessageType type() const {
            return m_type;
        }

        std::uint64_t timestamp() const {
            return m_timestamp;
        }

        TimestampType timestamp_type() const {
            return m_timestamp_type;
        }

        std::uint8_t signal() const {
            return m_signal;
        }

        const std::vector<std::uint8_t> &data() const {
            return m_data;
        }

        int df() const {
            switch (m_type) {
            case MessageType::MODE_S_SHORT:
            case MessageType::MODE_S_LONG:
                return (m_data[0] >> 3) & 31;
            default:
                return -1;
            }
        }

        bool crc_bad() const {
            switch (df()) {
            case 11:
                return (crc_residual() & 0xFFFF80) != 0;
            case 17:
            case 18:
                return crc_residual() != 0;
            default:
                return false;
            }
        }

    private:
        std::uint32_t crc_residual() const {
            if (residual == 0xFFFFFFFF) {
                std::size_t len = m_data.size();
                if (len <= 3) {
                    residual = 0;
                } else {
                    residual = crc(m_data.begin(), m_data.end() - 3);
                    residual ^= (m_data[len-3] << 16);
                    residual ^= (m_data[len-2] << 8);
                    residual ^= (m_data[len-1]);
                }
            }
            return residual;
        }

        MessageType m_type;
        TimestampType m_timestamp_type;
        std::uint64_t m_timestamp;
        std::uint8_t m_signal;
        std::vector<std::uint8_t> m_data;

        mutable std::uint32_t residual;
    };

    std::ostream& operator<<(std::ostream &os, const Message &message);
};

#endif
