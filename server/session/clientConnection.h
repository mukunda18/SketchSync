#ifndef SKETCHSYNC_CLIENTCONNECTION_H
#define SKETCHSYNC_CLIENTCONNECTION_H

#include "common/tcp/tcpSocket.h"
#include "common/webSocket/webSocket.h"

struct clientConnection
{
    tcpSocket* tcp = nullptr;
    webSocket* ws = nullptr;
    std::mutex write_mutex;

    result<size_t> send(std::span<const uint8_t> data)
    {
        std::lock_guard lock(write_mutex);
        if (tcp) return tcp->send(data);
        if (ws) return ws->send(data);
        return {.value = 0, .err = error::closed, .message = "no active connection"};
    }
};

#endif
