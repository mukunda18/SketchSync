#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "../results.h"

namespace beast = boost::beast;
namespace websocket_beast = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

struct websocket_server
{
    websocket_server(net::io_context& context, unsigned short port);

    websocket_server(const websocket_server&) = delete;
    websocket_server& operator=(const websocket_server&) = delete;
    websocket_server(websocket_server&&) = delete;
    websocket_server& operator=(websocket_server&&) = delete;

    ~websocket_server();

    result<websocket_beast::stream<tcp::socket>> accept();

    result<bool> close();

    [[nodiscard]] bool is_open() const noexcept;

private:
    tcp::acceptor acceptor_;
};

#endif