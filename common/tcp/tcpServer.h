#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "common/results.h"

namespace net = boost::asio;
using tcp = net::ip::tcp;

struct tcpServer
{
    tcpServer(net::io_context& context, unsigned short port);

    tcpServer(const tcpServer&) = delete;
    tcpServer& operator=(const tcpServer&) = delete;
    tcpServer(tcpServer&&) = delete;
    tcpServer& operator=(tcpServer&&) = delete;

    ~tcpServer();

    // Blocking accept: waits for and returns one connected peer socket
    result<tcp::socket> accept();

    result<bool> close();

    [[nodiscard]] bool is_open() const noexcept;

private:
    tcp::acceptor acceptor_;
};

#endif