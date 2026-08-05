#include "websocket.h"

websocket::websocket(net::io_context& context)
    : resolver_(context), ws_(context)
{
    ws_.binary(true);
}

websocket::websocket(webaddr address, net::io_context& context)
    : address_(std::move(address)), resolver_(context), ws_(context)
{
    ws_.binary(true);
}

websocket::~websocket()
{
    if (connected_) close();
}

result<bool> websocket::connect(webaddr address)
{
    address_ = std::move(address);
    return connect();
}

result<bool> websocket::connect()
{
    beast::error_code ec;

    const auto results = resolver_.resolve(address_.host, address_.port, ec);
    if (ec)
        return {.value = false, .error = error::resolve_failed, .message = ec.message()};

    net::connect(ws_.next_layer(), results, ec);
    if (ec)
        return {.value = false, .error = error::connect_failed, .message = ec.message()};

    ws_.handshake(address_.host, address_.path, ec);
    if (ec)
        return {.value = false, .error = error::handshake_failed, .message = ec.message()};

    ws_.binary(true);
    connected_ = true;
    return {.value = true, .error = error::none, .message = {}};
}

result<size_t> websocket::send(const std::span<const uint8_t> data)
{
    if (!connected_)
        return {.value = 0, .error = error::closed, .message = "socket not connected"};

    beast::error_code ec;
    const auto written = ws_.write(net::buffer(data.data(), data.size()), ec);

    if (ec)
        return {.value = 0, .error = error::send_failed, .message = ec.message()};

    return {.value = written, .error = error::none, .message = {}};
}

result<std::vector<uint8_t>> websocket::receive()
{
    if (!connected_)
        return {.value = {}, .error = error::closed, .message = "socket not connected"};

    beast::error_code ec;
    beast::flat_buffer buffer;

    ws_.read(buffer, ec);
    if (ec)
        return {.value = {}, .error = error::receive_failed, .message = ec.message()};

    const auto bytes = buffer.cdata();
    const auto* begin = static_cast<const uint8_t*>(bytes.data());

    return {.value = {begin, begin + bytes.size()}, .error = error::none, .message = {}};
}

void websocket::close()
{
    if (!connected_) return;

    beast::error_code ec;
    ws_.close(websocket_beast::close_code::normal, ec);
    connected_ = false;
}

bool websocket::is_open() const noexcept
{
    return connected_ && ws_.is_open();
}