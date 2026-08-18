#ifndef SKETCHSYNC_NETWORK_SESSION_STATE_H
#define SKETCHSYNC_NETWORK_SESSION_STATE_H

#include <string>
#include <cstdint>

enum class connection_protocol {
    none,
    tcp,
    websocket
};

enum class connection_state {
    disconnected,
    connecting,
    connected,
    failed,
    error_abort_close
};

enum class session_joining_state {
    none,
    creating,
    joining,
    in_session,
    leaving,
    closing
};

struct network_info {
    connection_protocol protocol = connection_protocol::tcp;
    std::string host = "127.0.0.1";
    std::string port = "9000";
    bool connected = false;
};

struct session_info {
    uint32_t session_id = 0;
    uint32_t member_id = 0;
    bool is_host = false;
    bool in_session = false;
    std::string session_id_input;
};

struct network_session_state {
    network_info net;
    session_info session;
    connection_state state = connection_state::disconnected;
    session_joining_state session_state = session_joining_state::none;
    std::string error_message;
    
    // Backward compatibility helpers
    connection_protocol protocol() const { return net.protocol; }
    std::string host() const { return net.host; }
    std::string port() const { return net.port; }
    bool connected() const { return net.connected; }
    uint32_t session_id() const { return session.session_id; }
    uint32_t member_id() const { return session.member_id; }
    bool is_host() const { return session.is_host; }
    bool in_session() const { return session.in_session; }
};

#endif
