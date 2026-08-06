#ifndef SKETCHSYNC_MEMBER_H
#define SKETCHSYNC_MEMBER_H

#include <cstdint>
#include <string>

enum class member_role : uint8_t
{
    participant,
    host
};

struct member
{
    uint32_t id;
    std::string name;
    member_role role = member_role::participant;
    uint32_t session_id = 0;

    [[nodiscard]] bool is_host() const noexcept
    {
        return role == member_role::host;
    }
};

#endif
