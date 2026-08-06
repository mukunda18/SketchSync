#include "webSocket.h"

webSocket::webSocket(net::io_context& context)
    : resolver_(context), ws_(context)
{
    ws_.binary(true);
}

webSocket::webSocket(webaddr address, net::io_context& context)
    : address_(std::move(address)), resolver_(context), ws_(context)
{
    ws_.binary(true);
}

webSocket::webSocket(websocket_beast::stream<tcp::socket> ws, net::io_context& context)
    : resolver_(context), ws_(std::move(ws))
{
    ws_.binary(true);
    connected_ = ws_.is_open();
}

webSocket::~webSocket()
{
    if (connected_) close();
}

result<bool> webSocket::connect(webaddr address)
{
    address_ = std::move(address);
    return connect();
}

result<bool> webSocket::connect()
{
    beast::error_code ec;

    const auto results = resolver_.resolve(address_.host, address_.port, ec);
    if (ec)
        return {.value = false, .err = error::resolve_failed, .message = ec.message()};

    net::connect(ws_.next_layer(), results, ec);
    if (ec)
        return {.value = false, .err = error::connect_failed, .message = ec.message()};

    ws_.handshake(address_.host, address_.path, ec);
    if (ec)
        return {.value = false, .err = error::handshake_failed, .message = ec.message()};

    ws_.binary(true);
    connected_ = true;
    return {.value = true, .err = error::none, .message = {}};
}

result<size_t> webSocket::send(const std::span<const uint8_t> data)
{
    if (!connected_)
        return {.value = 0, .err = error::closed, .message = "socket not connected"};

    beast::error_code ec;
    const auto written = ws_.write(net::buffer(data.data(), data.size()), ec);

    if (ec)
        return {.value = 0, .err = error::send_failed, .message = ec.message()};

    return {.value = written, .err = error::none, .message = {}};
}

result<std::vector<uint8_t>> webSocket::receive()
{
    if (!connected_)
        return {.value = {}, .err = error::closed, .message = "socket not connected"};

    beast::error_code ec;
    beast::flat_buffer buffer;

    ws_.read(buffer, ec);
    if (ec)
        return {.value = {}, .err = error::receive_failed, .message = ec.message()};

    const auto bytes = buffer.cdata();
    const auto* begin = static_cast<const uint8_t*>(bytes.data());

    return {.value = {begin, begin + bytes.size()}, .err = error::none, .message = {}};
}

result<bool> webSocket::close()
{
    if (!connected_)
        return {.value = true, .err = error::none, .message = {}};

    beast::error_code ec;
    ws_.close(websocket_beast::close_code::normal, ec);
    connected_ = false;

    if (ec)
        return {.value = false, .err = error::close_failed, .message = ec.message()};

    return {.value = true, .err = error::none, .message = {}};
}

bool webSocket::is_open() const noexcept
{
    return connected_ && ws_.is_open();
}