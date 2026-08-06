#ifndef webSocket_SERVER_H
#define webSocket_SERVER_H

#include <boost/beast/webSocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "common/results.h"

namespace beast = boost::beast;
namespace webSocket_beast = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

struct webSocketServer
{
    webSocketServer(net::io_context& context, unsigned short port);
    webSocketServer(const webSocketServer&) = delete;
    webSocketServer& operator=(const webSocketServer&) = delete;
    webSocketServer(webSocketServer&&) = delete;
    webSocketServer& operator=(webSocketServer&&) = delete;

    ~webSocketServer();

    result<webSocket_beast::stream<tcp::socket>> accept();

    result<bool> close();

    [[nodiscard]] bool is_open() const noexcept;

private:
    tcp::acceptor acceptor_;
};

#endif