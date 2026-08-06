#include "tcp_socket.h"
#include <boost/asio/connect.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

namespace net = boost::asio;
using tcp = net::ip::tcp;

tcp_socket::tcp_socket(net::io_context& context)
    : resolver_(context), socket_(context)
{}

tcp_socket::tcp_socket(tcp_addr address, net::io_context& context)
    : address_(std::move(address)), resolver_(context), socket_(context)
{}

tcp_socket::~tcp_socket()
{
    if (connected_) close();
}

result<bool> tcp_socket::connect(tcp_addr address)
{
    address_ = std::move(address);
    return connect();
}

result<bool> tcp_socket::connect()
{
    boost::system::error_code ec;

    const auto results = resolver_.resolve(address_.host, address_.port, ec);
    if (ec)
        return {.value = false, .error = error::resolve_failed, .message = ec.message()};

    net::connect(socket_, results, ec);
    if (ec)
        return {.value = false, .error = error::connect_failed, .message = ec.message()};

    connected_ = true;
    return {.value = true, .error = error::none, .message = {}};
}

result<size_t> tcp_socket::send(const std::span<const uint8_t> data)
{
    if (!connected_)
    {
        return {
            .value = 0,
            .error = error::closed,
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
            .error = error::send_failed,
            .message = ec.message()
        };
    }

    return {
        .value = written,
        .error = error::none,
        .message = {}
    };
}

result<std::vector<uint8_t>> tcp_socket::receive(const size_t length)
{
    if (!connected_)
    {
        return {
            .value = {},
            .error = error::closed,
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
            .error = error::receive_failed,
            .message = ec.message()
        };
    }

    return {
        .value = std::move(buffer),
        .error = error::none,
        .message = {}
    };
}

result<bool> tcp_socket::close()
{
    if (!connected_)
        return {.value = true, .error = error::none, .message = {}};

    boost::system::error_code ec;

    ec = socket_.shutdown(tcp::socket::shutdown_both, ec);
    if (ec)
        return {.value = false, .error = error::shutdown_failed, .message = ec.message()};

    ec = socket_.close(ec);
    connected_ = false;

    if (ec)
        return {.value = false, .error = error::close_failed, .message = ec.message()};

    return {.value = true, .error = error::none, .message = {}};
}

bool tcp_socket::is_open() const noexcept
{
    return connected_ && socket_.is_open();
}
