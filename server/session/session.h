#ifndef SKETCHSYNC_SESSION_H
#define SKETCHSYNC_SESSION_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <chrono>

#include "server/session/member.h"
#include "server/session/canvas.h"

enum class session_state : uint8_t
{
    open,
    locked,
    closed
};

struct session
{
    uint32_t session_id;
    uint32_t host_id;
    std::string name;

    session_state state = session_state::open;
    std::chrono::steady_clock::time_point created_at;

    std::unordered_map<uint32_t, member> members;
    mutable std::mutex members_mutex;

    canvas canvas_log;

    [[nodiscard]] bool has_member(const uint32_t id) const noexcept
    {
        std::lock_guard lock(members_mutex);
        return members.contains(id);
    }

    [[nodiscard]] bool is_host(const uint32_t id) const noexcept
    {
        return id == host_id;
    }

    [[nodiscard]] size_t member_count() const noexcept
    {
        std::lock_guard lock(members_mutex);
        return members.size();
    }
};

#endif