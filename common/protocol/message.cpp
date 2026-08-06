#include "message.h"
#include <cstring>
#include <winsock2.h>

result<Header> parseHeader(std::span<const uint8_t> data)
{
    if (data.size() < Header::SIZE)
    {
        return {
            .value = {},
            .error = error::malformed,
            .message = "not enough data for header"
        };
    }

    Header header;

    header.opcode = data[0];
    header.flags = data[1];

    uint32_t length;

    std::memcpy(
        &length,
        data.data() + 2,
        sizeof(length)
    );

    header.length = ntohl(length);

    return {
        .value = header,
        .error = error::none,
        .message = {}
    };
}


static std::vector<uint8_t> serializeMessage(const Message& message)
{
    std::vector<uint8_t> buffer(message.getSize());

    buffer[0] = message.header.opcode;
    buffer[1] = message.header.flags;

    uint32_t length = htonl(
        static_cast<uint32_t>(message.payload.size())
    );

    std::memcpy(
        buffer.data() + 2,
        &length,
        sizeof(length)
    );

    if (!message.payload.empty())
    {
        std::memcpy(
            buffer.data() + Header::SIZE,
            message.payload.data(),
            message.payload.size()
        );
    }

    return buffer;
}

