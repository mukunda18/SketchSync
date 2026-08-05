#ifndef TCP_SOCKET_H
#define TCP_SOCKET_H

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "result.h"

// Wire frame: [uint32_t body_len (big-endian)][uint8_t opcode][payload...]
struct tcp_message
{
    uint8_t opcode;
    std::vector<uint8_t> payload;
};

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

    result<size_t> send(uint8_t opcode, std::span<const uint8_t> payload);
    result<tcp_message> receive();

    void close();

    [[nodiscard]] bool is_open() const noexcept;

private:
    tcp_addr address_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;
    bool connected_ = false;
};

#endif
