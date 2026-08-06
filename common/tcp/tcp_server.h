#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "../results.h"

namespace net = boost::asio;
using tcp = net::ip::tcp;

struct tcp_server
{
    tcp_server(net::io_context& context, unsigned short port);

    tcp_server(const tcp_server&) = delete;
    tcp_server& operator=(const tcp_server&) = delete;
    tcp_server(tcp_server&&) = delete;
    tcp_server& operator=(tcp_server&&) = delete;

    ~tcp_server();

    // Blocking accept: waits for and returns one connected peer socket
    result<tcp::socket> accept();

    result<bool> close();

    [[nodiscard]] bool is_open() const noexcept;

private:
    tcp::acceptor acceptor_;
};

#endif