#ifndef TCP_SOCKET_H
#define TCP_SOCKET_H

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <string>
#include "common/results.h"
#include "common/protocol/message.h"

namespace net = boost::asio;
using tcp = net::ip::tcp;

struct tcp_addr
{
    std::string host;
    std::string port;
};

struct tcpSocket
{
    explicit tcpSocket(boost::asio::io_context& context);
    explicit tcpSocket(tcp_addr address, boost::asio::io_context& context);
    explicit tcpSocket(tcp::socket socket, net::io_context& context);

    tcpSocket(const tcpSocket&) = delete;
    tcpSocket& operator=(const tcpSocket&) = delete;
    tcpSocket(tcpSocket&&) = delete;
    tcpSocket& operator=(tcpSocket&&) = delete;

    ~tcpSocket();

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
