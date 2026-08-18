#ifndef webSocket_H
#define webSocket_H

#include <boost/beast/core.hpp>
#include <boost/beast/webSocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <span>
#include <string>
#include <vector>

#include "common/results.h"

namespace beast = boost::beast;
namespace websocket_beast = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

struct webaddr
{
    std::string host;
    std::string port;
    std::string path;
};

struct webSocket
{
    explicit webSocket(net::io_context& context);
    explicit webSocket(webaddr address, net::io_context& context);
    explicit webSocket(websocket_beast::stream<tcp::socket> ws, net::io_context& context);


    webSocket(const webSocket&) = delete;
    webSocket& operator=(const webSocket&) = delete;
    webSocket(webSocket&&) = delete;
    webSocket& operator=(webSocket&&) = delete;

    ~webSocket();

    result<bool> connect(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));
    result<bool> connect(webaddr address, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));

    result<size_t> send(std::span<const uint8_t> data);
    result<std::vector<uint8_t>> receive();

    result<bool> close();

    [[nodiscard]] bool is_open() const noexcept;

private:
    net::io_context& context_;
    webaddr address_;
    tcp::resolver resolver_;
    websocket_beast::stream<tcp::socket> ws_;
    bool connected_ = false;
};

#endif