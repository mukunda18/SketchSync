#include "tcpSocket.h"
#include <boost/asio/connect.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

namespace net = boost::asio;
using tcp = net::ip::tcp;

#include <chrono>
#include <boost/asio/async_result.hpp>

tcpSocket::tcpSocket(net::io_context& context)
    : context_(context), resolver_(context), socket_(context)
{}

tcpSocket::tcpSocket(tcp_addr address, net::io_context& context)
    : context_(context), address_(std::move(address)), resolver_(context), socket_(context)
{}

tcpSocket::tcpSocket(tcp::socket socket, net::io_context& context)
    : context_(context), resolver_(context), socket_(std::move(socket))
{
    connected_ = socket_.is_open();
}

tcpSocket::~tcpSocket()
{
    if (connected_) close();
}

result<bool> tcpSocket::connect(tcp_addr address, std::chrono::milliseconds timeout)
{
    address_ = std::move(address);
    return connect(timeout);
}

result<bool> tcpSocket::connect(std::chrono::milliseconds timeout)
{
    try
    {
        boost::system::error_code ec;

        const auto results = resolver_.resolve(address_.host, address_.port, ec);
        if (ec)
            return {.value = false, .err = error::resolve_failed, .message = ec.message()};

        bool connect_success = false;
        boost::system::error_code connect_ec;

        net::async_connect(socket_, results, [&](const boost::system::error_code& error, const tcp::endpoint&) {
            connect_ec = error;
            if (!error) connect_success = true;
        });

        context_.restart();
        context_.run_for(timeout);

        if (!connect_success)
        {
            socket_.close(ec);
            std::string msg = connect_ec ? connect_ec.message() : "Connection timed out";
            return {.value = false, .err = error::connect_failed, .message = std::move(msg)};
        }

        connected_ = true;
        return {.value = true, .err = error::none, .message = {}};
    }
    catch (const std::exception& ex)
    {
        return {.value = false, .err = error::connect_failed, .message = ex.what()};
    }
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
    boost::system::error_code ec;
    if (socket_.is_open())
    {
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }
    connected_ = false;
    return {.value = true, .err = error::none, .message = {}};
}

bool tcpSocket::is_open() const noexcept
{
    return connected_ && socket_.is_open();
}
