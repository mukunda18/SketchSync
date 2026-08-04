#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <span>
#include <string>
#include <vector>
#include "result.h"

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

struct websocket
{
    explicit websocket(net::io_context& context);
    explicit websocket(webaddr address, net::io_context& context);

    websocket(const websocket&) = delete;
    websocket& operator=(const websocket&) = delete;
    websocket(websocket&&) = delete;
    websocket& operator=(websocket&&) = delete;

    ~websocket();

    result<bool> connect();
    result<bool> connect(webaddr address);

    result<size_t> send(std::span<const uint8_t> data);
    result<std::vector<uint8_t>> receive();

    void close();

    [[nodiscard]] bool is_open() const noexcept;

private:
    webaddr address_;
    tcp::resolver resolver_;
    websocket_beast::stream<tcp::socket> ws_;
    bool connected_ = false;
};

#endif