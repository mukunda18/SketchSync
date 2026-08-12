#ifndef SKETCHSYNC_MESSAGE_H
#define SKETCHSYNC_MESSAGE_H
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "common/results.h"
#include "common/canvas/draw_operation.h"

namespace Opcode
{
    constexpr uint8_t CREATE = 0x00;
    constexpr uint8_t JOIN = 0x01;
    constexpr uint8_t LEAVE = 0x02;
    constexpr uint8_t ACK = 0x03;
    constexpr uint8_t ERROR_MSG = 0x04;
    constexpr uint8_t NOTIFICATION = 0x05;
    constexpr uint8_t DRAW = 0x06;
    constexpr uint8_t CANVAS_STATE = 0x07;
    constexpr uint8_t CANVAS_STATE_REQUEST = 0x08;
    constexpr uint8_t CLOSE_SESSION = 0x09;
}

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
std::vector<uint8_t> serializeMessage(const Message& message);


struct CreateMessage
{
    std::string name;
};

result<std::string> parseCreateMessage(std::span<const uint8_t> data);
std::vector<uint8_t> serializeCreateMessage(const CreateMessage& msg);


struct JoinMessage
{
    uint32_t session_id;
    std::string name;
};

result<JoinMessage> parseJoinMessage(std::span<const uint8_t> data);
std::vector<uint8_t> serializeJoinMessage(const JoinMessage& msg);


struct CreateAckMessage
{
    uint8_t ack_code;
    uint32_t session_id;
    uint32_t member_id;
};

std::vector<uint8_t> serializeCreateAckMessage(const CreateAckMessage& msg);
result<CreateAckMessage> parseCreateAckMessage(std::span<const uint8_t> data);


struct JoinAckMessage
{
    uint8_t ack_code;
    uint32_t member_id;
};

std::vector<uint8_t> serializeJoinAckMessage(const JoinAckMessage& msg);
result<JoinAckMessage> parseJoinAckMessage(std::span<const uint8_t> data);

namespace ackcode
{
    constexpr uint8_t OK = 0x00;
    constexpr uint8_t LEFT = 0x01;
    constexpr uint8_t CREATE_OK = 0x02;
    constexpr uint8_t JOIN_OK = 0x03;
}

struct AckMessage
{
    uint8_t ack_code;
    std::string message;
};

result<AckMessage> parseAckMessage(std::span<const uint8_t> data);
std::vector<uint8_t> serializeAckMessage(const AckMessage& message);

namespace errcode
{
    constexpr uint8_t UNKNOWN = 0x00;
    constexpr uint8_t CREATE_FAILED = 0x01;
    constexpr uint8_t JOIN_FAILED = 0x02;
    constexpr uint8_t LEAVE_FAILED = 0x03;
}

struct ErrorMessage
{
    uint8_t err_code;
    std::string err_message;
};

std::vector<uint8_t> serializeErrorMessage(const ErrorMessage& message);
result<ErrorMessage> parseErrorMessage(std::span<const uint8_t> data);

namespace notifcode
{
    constexpr uint8_t MEMBER_JOINED = 0x00;
    constexpr uint8_t MEMBER_LEFT = 0x01;
    constexpr uint8_t SESSION_CLOSED = 0x02;
}

struct MemberJoinedNotification
{
    uint8_t notif_code;
    uint32_t member_id;
    std::string name;
};

std::vector<uint8_t> serializeMemberJoinedNotification(const MemberJoinedNotification& notif);
result<MemberJoinedNotification> parseMemberJoinedNotification(std::span<const uint8_t> data);

struct MemberLeftNotification
{
    uint8_t notif_code;
    uint32_t member_id;
    std::string name;
};

std::vector<uint8_t> serializeMemberLeftNotification(const MemberLeftNotification& notif);
result<MemberLeftNotification> parseMemberLeftNotification(std::span<const uint8_t> data);

struct SessionClosedNotification
{
    uint8_t notif_code;
};

std::vector<uint8_t> serializeSessionClosedNotification(const SessionClosedNotification& notif);
result<SessionClosedNotification> parseSessionClosedNotification(std::span<const uint8_t> data);

struct CanvasStateMessage
{
    std::vector<draw_operation> operations;
};

std::vector<uint8_t> serializeCanvasStateMessage(const CanvasStateMessage& msg);
result<CanvasStateMessage> parseCanvasStateMessage(std::span<const uint8_t> data);

#endif
