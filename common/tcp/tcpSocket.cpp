#include "tcpSocket.h"
#include <boost/asio/connect.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

namespace net = boost::asio;
using tcp = net::ip::tcp;

tcpSocket::tcpSocket(net::io_context& context)
    : resolver_(context), socket_(context)
{}

tcpSocket::tcpSocket(tcp_addr address, net::io_context& context)
    : address_(std::move(address)), resolver_(context), socket_(context)
{}

tcpSocket::tcpSocket(tcp::socket socket, net::io_context& context)
    : resolver_(context), socket_(std::move(socket))
{
    connected_ = socket_.is_open();
}

tcpSocket::~tcpSocket()
{
    if (connected_) close();
}

result<bool> tcpSocket::connect(tcp_addr address)
{
    address_ = std::move(address);
    return connect();
}

result<bool> tcpSocket::connect()
{
    boost::system::error_code ec;

    const auto results = resolver_.resolve(address_.host, address_.port, ec);
    if (ec)
        return {.value = false, .err = error::resolve_failed, .message = ec.message()};

    net::connect(socket_, results, ec);
    if (ec)
        return {.value = false, .err = error::connect_failed, .message = ec.message()};

    connected_ = true;
    return {.value = true, .err = error::none, .message = {}};
}

result<size_t> tcpSocket::send(const std::span<const uint8_t> data)
{
    if (!connected_)
    {
        return {
            .value = 0,
            .err = error::closed,
            .message = "socket not connected"
        };
    }

    boost::system::error_code ec;

    const auto written = net::write(
        socket_,
        net::buffer(data.data(), data.size()),
        ec
    );

    if (ec)
    {
        return {
            .value = 0,
            .err = error::send_failed,
            .message = ec.message()
        };
    }

    return {
        .value = written,
        .err = error::none,
        .message = {}
    };
}

result<std::vector<uint8_t>> tcpSocket::receive(const size_t length)
{
    if (!connected_)
    {
        return {
            .value = {},
            .err = error::closed,
            .message = "socket not connected"
        };
    }

    std::vector<uint8_t> buffer(length);

    boost::system::error_code ec;

    net::read(
        socket_,
        net::buffer(buffer),
        ec
    );

    if (ec)
    {
        return {
            .value = {},
            .err = error::receive_failed,
            .message = ec.message()
        };
    }

    return {
        .value = std::move(buffer),
        .err = error::none,
        .message = {}
    };
}

result<bool> tcpSocket::close()
{
    if (!connected_)
        return {.value = true, .err = error::none, .message = {}};

    boost::system::error_code ec;

    ec = socket_.shutdown(tcp::socket::shutdown_both, ec);
    if (ec)
        return {.value = false, .err = error::shutdown_failed, .message = ec.message()};

    ec = socket_.close(ec);
    connected_ = false;

    if (ec)
        return {.value = false, .err = error::close_failed, .message = ec.message()};

    return {.value = true, .err = error::none, .message = {}};
}

bool tcpSocket::is_open() const noexcept
{
    return connected_ && socket_.is_open();
}
