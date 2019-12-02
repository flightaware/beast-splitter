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

#ifndef MODES_MESSAGE_H
#define MODES_MESSAGE_H

#include <cstdint>
#include <cassert>
#include <vector>
#include <ostream>
#include <functional>
#include <map>

#include "crc.h"

namespace modes {
    // the type of one message
    enum class MessageType {
        INVALID,
        MODE_AC,
        MODE_S_SHORT,
        MODE_S_LONG,
        STATUS,
        POSITION
    };

    enum class TimestampType { UNKNOWN, TWELVEMEG, GPS };

    inline std::ostream& operator<<(std::ostream &os, const MessageType &t) {
        switch (t) {
        case MessageType::MODE_AC: return (os << "MODE_AC");
        case MessageType::MODE_S_SHORT: return (os << "MODE_S_SHORT");
        case MessageType::MODE_S_LONG: return (os << "MODE_S_LONG");
        case MessageType::STATUS: return (os << "STATUS");
        case MessageType::POSITION: return (os << "POSITION");
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
        case MessageType::POSITION: return 14;
        default: return 0;
        }
    }

    // a single message
    class Message {
    public:
        Message()
            : m_type(MessageType::INVALID),
              m_timestamp_type(TimestampType::UNKNOWN),
              m_timestamp(0),
              m_signal(0)
        {}

        Message(MessageType type_,
                TimestampType timestamp_type_, std::uint64_t timestamp_,
                std::uint8_t signal_, std::vector<std::uint8_t> &&data_)
            : m_type(type_),
              m_timestamp_type(timestamp_type_),
              m_timestamp(timestamp_),
              m_signal(signal_),
              m_data(std::move(data_))
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
              m_residual(0xFFFFFFFF)
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

        bool crc_correctable() const {
            return (crc_correctable_bit() >= 0);
        }

        const std::vector<std::uint8_t> &corrected_data() const {
            if (!crc_bad()) {
                return m_data;
            }

            if (m_corrected_data.size() != 0) {
                // already corrected
                return m_corrected_data;
            }

            auto bit = crc_correctable_bit();
            if (bit < 0) {
                // not correctable
                return m_corrected_data; // empty vector
            }

            // copy the original data and do FEC
            m_corrected_data = m_data;
            m_corrected_data[bit/8] ^= (1 << (7 - (bit & 7)));
            return m_corrected_data;
        }

    private:
        std::uint32_t crc_residual() const {
            if (m_residual == 0xFFFFFFFF) {
                m_residual = crc::message_residual(m_data);
            }
            return m_residual;
        }

        int crc_correctable_bit() const {
            if (m_correctable_bit == -2) {
                std::uint32_t residual;
                switch (df()) {
                case 11:
                    // For DF11, don't mask off the lower 7 bits
                    // i.e. try to correct under the assumption that IID=0
                case 17:
                case 18:
                    residual = crc_residual();
                default:
                    return false;
                }

                m_correctable_bit = crc::correctable_bit(residual);
                if (m_correctable_bit >= (int)m_data.size())
                    m_correctable_bit = -1;
            }

            return m_correctable_bit;
        }

        MessageType m_type;
        TimestampType m_timestamp_type;
        std::uint64_t m_timestamp;
        std::uint8_t m_signal;
        std::vector<std::uint8_t> m_data;

        mutable std::uint32_t m_residual = 0xFFFFFFFF;
        mutable int m_correctable_bit = -2;
        mutable std::vector<std::uint8_t> m_corrected_data;
    };

    std::ostream& operator<<(std::ostream &os, const Message &message);
};

#endif
