#ifndef SKETCHSYNC_SERVER_H
#define SKETCHSYNC_SERVER_H

#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <span>

#include "common/tcp/tcpServer.h"
#include "common/tcp/tcpSocket.h"
#include "common/webSocket/webSocketServer.h"
#include "common/webSocket/webSocket.h"
#include "server/session/clientConnection.h"
#include "server/session/session.h"

struct clientContext
{
    uint32_t member_id = 0;
    uint32_t session_id = 0;
};

struct client_thread
{
    std::thread thread;
    std::atomic<bool> stop_flag{false};
};

struct server
{
    server(net::io_context& io, unsigned short webSocket_port, unsigned short tcp_server_port);

    server(const server&) = delete;
    server& operator=(const server&) = delete;
    server(server&&) = delete;
    server& operator=(server&&) = delete;

    ~server();

    void run();
    void shutdown();

private:
    void run_tcp_accept_loop();
    void run_ws_accept_loop();

    void handle_tcp_client(const std::atomic<bool>& stop_flag,
                           tcp::socket raw_socket);
    void handle_ws_client(const std::atomic<bool>& stop_flag,
                          websocket_beast::stream<tcp::socket> raw_ws);

    void dispatch(Header header,
                  std::span<const uint8_t> payload,
                  clientContext& ctx,
                  clientConnection& conn);


    result<bool> handle_create(uint32_t session_id, const std::string& name,
                               clientContext& client_context);
    result<bool> handle_join(uint32_t session_id, const std::string& name,
                             clientContext& ctx);
    result<bool> handle_leave(clientContext& ctx);
    result<bool> handle_close_session(clientContext& ctx);
    void handle_draw(std::span<const uint8_t> payload,
                     const clientContext& ctx,
                     clientConnection& conn);
    void handle_canvas_state_request(const clientContext& ctx, clientConnection& conn);
    void handle_canvas_state(std::span<const uint8_t> payload,
                             const clientContext& ctx,
                             clientConnection& conn);

    static void sendAck(clientConnection& conn, uint8_t ack_code, const std::string& message_text);
    static void sendError(clientConnection& conn, uint8_t err_code, const std::string& message_text);
    void sendNotification(const session& sess,
                          uint8_t opcode,
                          const std::vector<uint8_t>& payload,
                          uint32_t exclude_id = 0);

    result<bool> broadcast_to_session(const session& sess,
                                      std::span<const uint8_t> data,
                                      uint32_t exclude_member_id = 0);

    net::io_context& io_;
    tcpServer tcp_server;
    webSocketServer webSocket_server;

    std::atomic<bool> accept_stop_flag{false};
    std::thread tcp_accept_thread;
    std::thread ws_accept_thread;

    std::mutex clients_mutex;
    std::vector<std::unique_ptr<client_thread>> client_threads;

    std::mutex sessions_mutex;
    std::unordered_map<uint32_t, session> sessions;
    session* find_session(uint32_t session_id);

    std::mutex connections_mutex;
    std::unordered_map<uint32_t, clientConnection*> connections;

    clientConnection* find_connection(uint32_t member_id);
    void register_connection(uint32_t member_id, clientConnection* conn);
    void unregister_connection(uint32_t member_id);

    std::atomic<uint32_t> next_session_id{1};
    std::atomic<uint32_t> next_member_id{1};
};

#endif
