#ifndef SKETCHSYNC_SESSIONCLIENT_H
#define SKETCHSYNC_SESSIONCLIENT_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "common/results.h"
#include "common/protocol/message.h"
#include "common/canvas/draw_operation.h"
#include "common/tcp/tcpSocket.h"
#include "common/websocket/websocket.h"
#include "server/session/clientConnection.h"

struct remote_member
{
    uint32_t id;
    std::string name;
};

struct sessionClient
{
    explicit sessionClient(tcpSocket& tcp);
    explicit sessionClient(webSocket& ws);

    sessionClient(const sessionClient&) = delete;
    sessionClient& operator=(const sessionClient&) = delete;
    sessionClient(sessionClient&&) = delete;
    sessionClient& operator=(sessionClient&&) = delete;

    // Returns the assigned session_id on success
    result<uint32_t> create(const std::string& name);
    result<bool>     join(uint32_t session_id, const std::string& name);
    result<bool>     leave();

    // Sends a draw operation; member_id and seq are filled in automatically
    result<bool> send_draw(draw_operation op);
    result<bool> request_canvas_state();

    // Receives and dispatches one incoming notification; call in a loop
    result<bool> poll();

    std::function<void(uint32_t id, const std::string& name)> on_member_joined;
    std::function<void(uint32_t id, const std::string& name)> on_member_left;
    std::function<void()> on_session_closed;
    std::function<void(const draw_operation&)> on_draw;
    std::function<void(const std::vector<draw_operation>&)> on_canvas_state;

    [[nodiscard]] uint32_t member_id()  const noexcept { return member_id_; }
    [[nodiscard]] uint32_t session_id() const noexcept { return session_id_; }
    [[nodiscard]] bool     in_session() const noexcept { return session_id_ != 0; }

    [[nodiscard]] const std::unordered_map<uint32_t, remote_member>& members() const noexcept { return members_; }
    [[nodiscard]] const std::vector<draw_operation>& canvas_log() const noexcept { return canvas_log_; }

private:
    result<Message> receive_one();
    result<size_t>  send_message(const Message& msg);

    clientConnection conn_;
    uint32_t member_id_  = 0;
    uint32_t session_id_ = 0;
    std::unordered_map<uint32_t, remote_member> members_;
    std::vector<draw_operation> canvas_log_;
};

#endif
