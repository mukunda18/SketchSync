#ifndef TCP_SOCKET_H
#define TCP_SOCKET_H

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <string>
#include "../results.h"
#include "../protocol/message.h"

namespace net = boost::asio;
using tcp = net::ip::tcp;

struct tcp_addr
{
    std::string host;
    std::string port;
};

struct tcp_socket
{
    explicit tcp_socket(boost::asio::io_context& context);
    explicit tcp_socket(tcp_addr address, boost::asio::io_context& context);

    tcp_socket(const tcp_socket&) = delete;
    tcp_socket& operator=(const tcp_socket&) = delete;
    tcp_socket(tcp_socket&&) = delete;
    tcp_socket& operator=(tcp_socket&&) = delete;

    ~tcp_socket();

    result<bool> connect();
    result<bool> connect(tcp_addr address);

    result<size_t> send(std::span<const uint8_t> data);
    result<std::vector<uint8_t>> receive(size_t length);

    result<bool> close();

    [[nodiscard]] bool is_open() const noexcept;

private:
    tcp_addr address_;
    tcp::resolver resolver_;
    net::ip::tcp::socket socket_;
    bool connected_ = false;
};

#endif
