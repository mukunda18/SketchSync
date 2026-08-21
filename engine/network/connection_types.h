#ifndef SKETCHSYNC_CONNECTION_TYPES_H
#define SKETCHSYNC_CONNECTION_TYPES_H

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

#endif
