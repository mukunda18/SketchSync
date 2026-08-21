#ifndef SKETCHSYNC_SESSION_MANAGER_H
#define SKETCHSYNC_SESSION_MANAGER_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "common/canvas/draw_operation.h"
#include "common/protocol/message.h"

struct canvas;
struct file_manager;
struct network_manager;
struct sessionClient;

enum class session_joining_state {
    none,
    creating,
    joining,
    in_session,
    leaving,
    closing
};

struct session_manager
{
    session_manager(canvas& surface,
                    file_manager& files,
                    network_manager& net,
                    std::atomic<bool>& dirty,
                    std::function<void(std::string)> set_status);
    ~session_manager();

    session_manager(const session_manager&) = delete;
    session_manager& operator=(const session_manager&) = delete;

    void join_session();
    void create_session();
    void leave_session();
    void reset_session();
    void clear_joining_state();
    void prepare_join(uint32_t session_id);
    void prepare_create();
    void send_join_request(uint32_t session_id);
    void send_create_request();

    void attach_client(std::unique_ptr<sessionClient> client);
    void detach_client();
    void start_poll();
    void join_poll_thread();
    [[nodiscard]] bool has_client() const;

    void send_leave_or_close() const;
    void broadcast_draw(const draw_operation& op, bool track_pending);
    [[nodiscard]] uint32_t member_id_or(uint32_t fallback) const;
    [[nodiscard]] bool host_owns_canvas() const;
    [[nodiscard]] bool in_session() const;
    [[nodiscard]] bool is_host() const;
    [[nodiscard]] uint32_t session_id() const;
    [[nodiscard]] uint32_t member_id() const;
    std::string& session_id_input();

    uint64_t next_operation_id();

private:
    void poll_session();
    void handle_notification(const std::vector<uint8_t>& payload);
    void handle_draw(const std::vector<uint8_t>& payload);
    void handle_canvas_state(const std::vector<uint8_t>& payload) const;
    void handle_ack(const Message& msg);
    void handle_error(const std::vector<uint8_t>& payload);

    canvas& surface_;
    file_manager& files_;
    network_manager& net_;
    std::atomic<bool>& dirty_;
    std::function<void(std::string)> set_status_;

    std::unique_ptr<sessionClient> client_;
    std::thread poll_thread_;

    uint32_t session_id_ = 0;
    uint32_t member_id_ = 0;
    bool is_host_ = false;
    bool in_session_ = false;
    std::string session_id_input_;
    session_joining_state joining_state_ = session_joining_state::none;

    std::atomic<uint32_t> next_operation_number_{1};
    std::unordered_set<uint64_t> pending_operations_;
    mutable std::mutex pending_mutex_;
    mutable std::mutex session_mutex_;
};

#endif
