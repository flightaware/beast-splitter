// -*- c++ -*-

#ifndef BEAST_MESSAGE_H
#define BEAST_MESSAGE_H

#include <cstdint>
#include <vector>
#include <ostream>

namespace beastsplitter {
    namespace message {
        // the type of one message
        enum class MessageType : std::uint8_t {
            INVALID = 0, MODE_AC = 0x31,
            MODE_S_SHORT = 0x32,
            MODE_S_LONG = 0x33,
            STATUS  = 0x34
        };
        
        inline std::uint8_t messagetype_to_byte(MessageType t) {
            return static_cast<std::uint8_t>(t);
        }

        inline std::ostream& operator<<(std::ostream &os, const MessageType &t) {
            switch (t) {
            case MessageType::MODE_AC: return (os << "MODE_AC");
            case MessageType::MODE_S_SHORT: return (os << "MODE_S_SHORT");
            case MessageType::MODE_S_LONG: return (os << "MODE_S_LONG");
            case MessageType::STATUS: return (os << "STATUS");
            default: return (os << "INVALID");
            }
        }
        
        MessageType messagetype_from_byte(std::uint8_t b);
        std::uint8_t messagetype_to_byte(MessageType type);
        std::size_t messagetype_length(MessageType type);
    
        // a single message
        struct Message {
            MessageType type;
            std::uint64_t timestamp;
            std::uint8_t signal;
            std::vector<std::uint8_t> data;
        };
    };
};

#endif
