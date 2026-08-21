#include "common/udp/udpSocket.h"
#include <boost/asio/steady_timer.hpp>

udpSocket::udpSocket(net::io_context& context)
    : context_(context), resolver_(context), socket_(context)
{
}

udpSocket::udpSocket(udp_addr address, net::io_context& context)
    : context_(context), address_(std::move(address)), resolver_(context), socket_(context)
{
}

udpSocket::udpSocket(net::io_context& context, const unsigned short bind_port, const bool broadcast,
                     const bool reuse_addr)
    : context_(context), resolver_(context), socket_(context)
{
    bind(bind_port, broadcast, reuse_addr);
}

udpSocket::~udpSocket()
{
    close();
}

result<bool> udpSocket::open()
{
    if (!socket_.is_open())
    {
        boost::system::error_code ec;
        (void)socket_.open(udp::v4(), ec);
        if (ec)
            return {.value = false, .err = error::connect_failed, .message = ec.message()};
    }
    return {.value = true, .err = error::none, .message = {}};
}

result<bool> udpSocket::bind(const unsigned short port, const bool broadcast, const bool reuse_addr)
{
    boost::system::error_code ec;
    if (!socket_.is_open())
    {
        (void)socket_.open(udp::v4(), ec);
        if (ec)
            return {.value = false, .err = error::connect_failed, .message = ec.message()};
    }

    if (reuse_addr)
    {
        (void)socket_.set_option(boost::asio::socket_base::reuse_address(true), ec);
        if (ec)
            return {.value = false, .err = error::connect_failed, .message = ec.message()};
    }
    if (broadcast)
    {
        (void)socket_.set_option(boost::asio::socket_base::broadcast(true), ec);
        if (ec)
            return {.value = false, .err = error::connect_failed, .message = ec.message()};
    }

    (void)socket_.bind(udp::endpoint(udp::v4(), port), ec);
    if (ec)
        return {.value = false, .err = error::connect_failed, .message = ec.message()};

    return {.value = true, .err = error::none, .message = {}};
}

result<bool> udpSocket::set_broadcast(const bool enable)
{
    boost::system::error_code ec;
    if (!socket_.is_open())
    {
        if (auto open_res = open(); !open_res) return open_res;
    }
    (void)socket_.set_option(boost::asio::socket_base::broadcast(enable), ec);
    if (ec)
        return {.value = false, .err = error::send_failed, .message = ec.message()};
    return {.value = true, .err = error::none, .message = {}};
}

result<bool> udpSocket::set_reuse_address(const bool enable)
{
    boost::system::error_code ec;
    if (!socket_.is_open())
    {
        if (auto open_res = open(); !open_res) return open_res;
    }
    (void)socket_.set_option(boost::asio::socket_base::reuse_address(enable), ec);
    if (ec)
        return {.value = false, .err = error::connect_failed, .message = ec.message()};
    return {.value = true, .err = error::none, .message = {}};
}

result<size_t> udpSocket::send_to(const std::span<const uint8_t> data, const std::string& host,
                                  const unsigned short port)
{
    boost::system::error_code ec;
    if (!socket_.is_open())
    {
        if (const auto open_res = open(); !open_res)
            return {.value = 0, .err = open_res.err, .message = open_res.message};
    }

    const auto endpoints = resolver_.resolve(host, std::to_string(port), ec);
    if (ec || endpoints.empty())
        return {.value = 0, .err = error::resolve_failed, .message = ec.message()};

    const size_t bytes = socket_.send_to(net::buffer(data.data(), data.size()), *endpoints.begin(), 0, ec);
    if (ec)
        return {.value = 0, .err = error::send_failed, .message = ec.message()};

    return {.value = bytes, .err = error::none, .message = {}};
}

result<size_t> udpSocket::send_broadcast(const std::span<const uint8_t> data, const unsigned short port)
{
    boost::system::error_code ec;
    if (!socket_.is_open())
    {
        if (const auto open_res = open(); !open_res)
            return {.value = 0, .err = open_res.err, .message = open_res.message};
    }
    if (const auto broadcast_res = set_broadcast(true); !broadcast_res) return {
        .value = 0, .err = broadcast_res.err, .message = broadcast_res.message
    };

    const udp::endpoint broadcast_ep(boost::asio::ip::address_v4::broadcast(), port);
    const size_t bytes = socket_.send_to(net::buffer(data.data(), data.size()), broadcast_ep, 0, ec);
    if (ec)
        return {.value = 0, .err = error::send_failed, .message = ec.message()};

    return {.value = bytes, .err = error::none, .message = {}};
}

result<size_t> udpSocket::send_to(const std::span<const uint8_t> data, const udp::endpoint& endpoint)
{
    boost::system::error_code ec;
    if (!socket_.is_open())
    {
        if (const auto open_res = open(); !open_res)
            return {.value = 0, .err = open_res.err, .message = open_res.message};
    }

    const size_t bytes = socket_.send_to(net::buffer(data.data(), data.size()), endpoint, 0, ec);
    if (ec)
        return {.value = 0, .err = error::send_failed, .message = ec.message()};

    return {.value = bytes, .err = error::none, .message = {}};
}

result<udp_packet> udpSocket::receive_from(const size_t max_size)
{
    if (!socket_.is_open())
        return {.err = error::closed, .message = "Socket not open"};

    std::vector<uint8_t> buf(max_size);
    udp::endpoint sender_ep;
    boost::system::error_code ec;

    const size_t bytes = socket_.receive_from(net::buffer(buf), sender_ep, 0, ec);
    if (ec)
        return {.err = error::receive_failed, .message = ec.message()};

    buf.resize(bytes);
    std::string ip = sender_ep.address().to_string();
    if (sender_ep.address().is_loopback() || sender_ep.address().is_unspecified())
    {
        ip = "127.0.0.1";
    }

    return {
        .value = udp_packet{
            .data = std::move(buf),
            .sender_ip = std::move(ip),
            .sender_port = sender_ep.port(),
            .sender_endpoint = sender_ep
        },
        .err = error::none,
        .message = {}
    };
}

result<udp_packet> udpSocket::receive_from(const std::chrono::milliseconds timeout, const size_t max_size)
{
    if (!socket_.is_open())
        return {.err = error::closed, .message = "Socket not open"};

    std::vector<uint8_t> buf(max_size);
    udp::endpoint sender_ep;
    bool received = false;
    boost::system::error_code recv_ec;
    size_t bytes_received = 0;

    net::steady_timer timer(context_);
    timer.expires_after(timeout);

    socket_.async_receive_from(net::buffer(buf), sender_ep, [&](const boost::system::error_code& ec, const size_t bytes)
    {
        recv_ec = ec;
        bytes_received = bytes;
        if (!ec) received = true;
        timer.cancel();
    });

    timer.async_wait([&](const boost::system::error_code& ec)
    {
        if (!ec && !received)
        {
            boost::system::error_code cancel_ec;
            (void)socket_.cancel(cancel_ec);
        }
    });

    context_.restart();
    context_.run_for(timeout + std::chrono::milliseconds(50));

    if (!received)
    {
        std::string msg = recv_ec ? recv_ec.message() : "UDP receive timed out";
        return {.err = error::receive_failed, .message = std::move(msg)};
    }

    buf.resize(bytes_received);
    std::string ip = sender_ep.address().to_string();
    if (sender_ep.address().is_loopback() || sender_ep.address().is_unspecified())
    {
        ip = "127.0.0.1";
    }

    return {
        .value = udp_packet{
            .data = std::move(buf),
            .sender_ip = std::move(ip),
            .sender_port = sender_ep.port(),
            .sender_endpoint = sender_ep
        },
        .err = error::none,
        .message = {}
    };
}

result<bool> udpSocket::close()
{
    if (socket_.is_open())
    {
        boost::system::error_code cancel_ec;
        (void)socket_.cancel(cancel_ec);

        boost::system::error_code close_ec;
        (void)socket_.close(close_ec);
    }
    return {.value = true, .err = error::none, .message = {}};
}

bool udpSocket::is_open() const noexcept
{
    return socket_.is_open();
}
