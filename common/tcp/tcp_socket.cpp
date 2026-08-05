#include "tcp_socket.h"
#include <boost/asio/connect.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <cstring>

namespace net = boost::asio;
using tcp = net::ip::tcp;

static uint32_t encode_u32_be(uint32_t v)
{
    return ((v & 0xFF) << 24) | (((v >> 8) & 0xFF) << 16) | (((v >> 16) & 0xFF) << 8) | ((v >> 24) & 0xFF);
}

static uint32_t decode_u32_be(const uint8_t* buf)
{
    return (uint32_t(buf[0]) << 24) | (uint32_t(buf[1]) << 16) | (uint32_t(buf[2]) << 8) | uint32_t(buf[3]);
}

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
        return {.value = false, .error = tcp_error::resolve_failed, .message = ec.message()};

    net::connect(socket_, results, ec);
    if (ec)
        return {.value = false, .error = tcp_error::connect_failed, .message = ec.message()};

    connected_ = true;
    return {.value = true, .error = tcp_error::none, .message = {}};
}

result<size_t> tcp_socket::send(uint8_t opcode, std::span<const uint8_t> payload)
{
    if (!connected_)
        return {.value = 0, .error = tcp_error::closed, .message = "socket not connected"};

    // Body = opcode byte + payload; length prefix covers the entire body
    const uint32_t body_len = static_cast<uint32_t>(1 + payload.size());
    const uint32_t net_len = encode_u32_be(body_len);

    std::vector<uint8_t> frame(4 + 1 + payload.size());
    std::memcpy(frame.data(), &net_len, 4);
    frame[4] = opcode;
    if (!payload.empty())
        std::memcpy(frame.data() + 5, payload.data(), payload.size());

    boost::system::error_code ec;
    const auto written = net::write(socket_, net::buffer(frame), ec);
    if (ec)
        return {.value = 0, .error = tcp_error::send_failed, .message = ec.message()};

    return {.value = written, .error = tcp_error::none, .message = {}};
}

result<tcp_message> tcp_socket::receive()
{
    if (!connected_)
        return {.value = {}, .error = tcp_error::closed, .message = "socket not connected"};

    boost::system::error_code ec;

    uint8_t header[4];
    net::read(socket_, net::buffer(header, 4), ec);
    if (ec)
        return {.value = {}, .error = tcp_error::receive_failed, .message = ec.message()};

    const uint32_t body_len = decode_u32_be(header);
    if (body_len == 0)
        return {.value = {}, .error = tcp_error::malformed, .message = "zero-length body"};

    std::vector<uint8_t> body(body_len);
    net::read(socket_, net::buffer(body), ec);
    if (ec)
        return {.value = {}, .error = tcp_error::receive_failed, .message = ec.message()};

    tcp_message msg;
    msg.opcode = body[0];
    msg.payload.assign(body.begin() + 1, body.end());

    return {.value = std::move(msg), .error = tcp_error::none, .message = {}};
}

void tcp_socket::close()
{
    if (!connected_) return;

    boost::system::error_code ec;
    socket_.shutdown(tcp::socket::shutdown_both, ec);
    socket_.close(ec);
    connected_ = false;
}

bool tcp_socket::is_open() const noexcept
{
    return connected_ && socket_.is_open();
}
