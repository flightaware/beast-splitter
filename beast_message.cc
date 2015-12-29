#include "beast_message.h"

using namespace beastsplitter::message;

MessageType beastsplitter::message::messagetype_from_byte(std::uint8_t b)
{
    switch (b) {
    case 0x31: return MessageType::MODE_AC;
    case 0x32: return MessageType::MODE_S_SHORT;
    case 0x33: return MessageType::MODE_S_LONG;
    case 0x34: return MessageType::STATUS;
    default: return MessageType::INVALID;
    }
}

std::size_t beastsplitter::message::messagetype_length(MessageType type)
{
    // return the expected number of data bytes for the message,
    // after any doubled 1As are removed.
    //
    //  6 bytes timestamp
    //  1 byte signal level
    //  2, 7, or 14 bytes of message data

    switch (type) {
    case MessageType::MODE_AC: return 6 + 1 + 2;
    case MessageType::MODE_S_SHORT: return 6 + 1 + 7;
    case MessageType::MODE_S_LONG: return 6 + 1 + 14;
    case MessageType::STATUS: return 6 + 1 + 14;
    default: return 0;
    }
}

