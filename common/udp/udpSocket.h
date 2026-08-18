#ifndef SKETCHSYNC_UDP_SOCKET_H
#define SKETCHSYNC_UDP_SOCKET_H

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <chrono>
#include <string>
#include <vector>
#include <span>
#include "common/results.h"

namespace net = boost::asio;
using udp = net::ip::udp;

struct udp_addr
{
    std::string host;
    std::string port;
};

struct udp_packet
{
    std::vector<uint8_t> data;
    std::string sender_ip;
    unsigned short sender_port = 0;
    udp::endpoint sender_endpoint;
};

struct udpSocket
{
    explicit udpSocket(net::io_context& context);
    explicit udpSocket(udp_addr address, net::io_context& context);
    explicit udpSocket(net::io_context& context, unsigned short bind_port, bool broadcast = true, bool reuse_addr = true);

    udpSocket(const udpSocket&) = delete;
    udpSocket& operator=(const udpSocket&) = delete;
    udpSocket(udpSocket&&) = delete;
    udpSocket& operator=(udpSocket&&) = delete;

    ~udpSocket();

    result<bool> open();
    result<bool> bind(unsigned short port, bool broadcast = true, bool reuse_addr = true);
    result<bool> set_broadcast(bool enable);
    result<bool> set_reuse_address(bool enable);

    result<size_t> send_to(std::span<const uint8_t> data, const std::string& host, unsigned short port);
    result<size_t> send_broadcast(std::span<const uint8_t> data, unsigned short port);
    result<size_t> send_to(std::span<const uint8_t> data, const udp::endpoint& endpoint);

    result<udp_packet> receive_from(size_t max_size = 512);
    result<udp_packet> receive_from(std::chrono::milliseconds timeout, size_t max_size = 512);

    result<bool> close();
    [[nodiscard]] bool is_open() const noexcept;

    udp::socket& raw_socket() noexcept { return socket_; }

private:
    net::io_context& context_;
    udp_addr address_;
    udp::resolver resolver_;
    udp::socket socket_;
};

#endif // SKETCHSYNC_UDP_SOCKET_H
