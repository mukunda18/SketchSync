#ifndef SKETCHSYNC_SESSIONCLIENT_H
#define SKETCHSYNC_SESSIONCLIENT_H

#include <vector>
#include <string>
#include "common/results.h"
#include "common/protocol/message.h"
#include "common/canvas/draw_operation.h"
#include "common/tcp/tcpSocket.h"
#include "common/websocket/websocket.h"
#include "server/session/clientConnection.h"

struct sessionClient
{
    explicit sessionClient(tcpSocket& tcp);
    explicit sessionClient(webSocket& ws);

    sessionClient(const sessionClient&) = delete;
    sessionClient& operator=(const sessionClient&) = delete;
    sessionClient(sessionClient&&) = delete;
    sessionClient& operator=(sessionClient&&) = delete;

    // Returns the assigned session_id on success
    result<bool> send_create(const std::string& name);
    result<bool> send_join(uint32_t session_id, const std::string& name);
    result<bool> send_leave();
    result<bool> send_close_session();

    // Sends a draw operation; member_id is filled in automatically.
    result<bool> send_draw(draw_operation op);
    result<bool> send_draw_raw(const draw_operation& op);
    result<bool> send_canvas_state(const std::vector<draw_operation>& operations);
    result<bool> request_canvas_state();

    // Receives one incoming message; engine handles dispatch.
    [[nodiscard]] result<Message> poll() const;

    [[nodiscard]] uint32_t member_id() const noexcept { return member_id_; }
    [[nodiscard]] uint32_t session_id() const noexcept { return session_id_; }
    [[nodiscard]] bool in_session() const noexcept { return session_id_ != 0; }
    [[nodiscard]] bool is_host() const noexcept { return host_; }
    void mark_session_closed() noexcept { member_id_ = 0; session_id_ = 0; host_ = false; }
    void set_session_info(const uint32_t member_id, const uint32_t session_id, const bool host) {
        member_id_ = member_id;
        session_id_ = session_id;
        host_ = host;
    }

private:
    [[nodiscard]] result<Message> receive_msg() const;
    result<size_t> send_message(const Message& msg);

    clientConnection conn_;
    uint32_t member_id_ = 0;
    uint32_t session_id_ = 0;
    bool host_ = false;
};

#endif
