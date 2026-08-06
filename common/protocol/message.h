#ifndef SKETCHSYNC_MESSAGE_H
#define SKETCHSYNC_MESSAGE_H
#include <cstdint>
#include <span>
#include <vector>

#include "../results.h"


struct Header
{
    uint8_t opcode;
    uint8_t flags;
    uint32_t length;

    static constexpr size_t SIZE = 6;
};

struct Message
{
    Header header;
    std::vector<uint8_t> payload;

    [[nodiscard]] size_t getSize() const noexcept { return Header::SIZE + payload.size(); }
};

result<Header> parseHeader(std::span<const uint8_t> data);

std::vector<uint8_t> serializeMessage(Message& message);

#endif
