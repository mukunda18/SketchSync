#ifndef SKETCHSYNC_NETWORK_SESSION_STATE_H
#define SKETCHSYNC_NETWORK_SESSION_STATE_H

#include <string>
#include <cstdint>
#include "common/network_constants.h"

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
    std::string host = std::string(net_config::DEFAULT_HOST);
    std::string port = std::string(net_config::DEFAULT_TCP_PORT_STR);
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
};

#endif
