// -*- c++ -*-

#ifndef BEAST_MESSAGE_H
#define BEAST_MESSAGE_H

#include <cstdint>
#include <vector>

namespace beastsplitter {
    namespace message {
        // the type of one message
        enum class MessageType {  INVALID, MODE_AC, MODE_S_SHORT, MODE_S_LONG, STATUS };
        
        MessageType messagetype_from_byte(std::uint8_t b);
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
