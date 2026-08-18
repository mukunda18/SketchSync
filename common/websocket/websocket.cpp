#include "webSocket.h"

#include <chrono>

webSocket::webSocket(net::io_context& context)
    : context_(context), resolver_(context), ws_(context)
{
    ws_.binary(true);
}

webSocket::webSocket(webaddr address, net::io_context& context)
    : context_(context), address_(std::move(address)), resolver_(context), ws_(context)
{
    ws_.binary(true);
}

webSocket::webSocket(websocket_beast::stream<tcp::socket> ws, net::io_context& context)
    : context_(context), resolver_(context), ws_(std::move(ws))
{
    ws_.binary(true);
    connected_ = ws_.is_open();
}

webSocket::~webSocket()
{
    if (connected_) close();
}

result<bool> webSocket::connect(webaddr address, std::chrono::milliseconds timeout)
{
    address_ = std::move(address);
    return connect(timeout);
}

result<bool> webSocket::connect(std::chrono::milliseconds timeout)
{
    try
    {
        beast::error_code ec;

        const auto results = resolver_.resolve(address_.host, address_.port, ec);
        if (ec)
            return {.value = false, .err = error::resolve_failed, .message = ec.message()};

        bool connect_success = false;
        boost::system::error_code connect_ec;

        net::async_connect(ws_.next_layer(), results, [&](const boost::system::error_code& error, const tcp::endpoint&) {
            connect_ec = error;
            if (!error) connect_success = true;
        });

        context_.restart();
        context_.run_for(timeout);

        if (!connect_success)
        {
            ws_.next_layer().close(ec);
            std::string msg = connect_ec ? connect_ec.message() : "Connection timed out";
            return {.value = false, .err = error::connect_failed, .message = std::move(msg)};
        }

        websocket_beast::stream_base::timeout opt{
            timeout,
            websocket_beast::stream_base::none(),
            false
        };
        ws_.set_option(opt);

        bool handshake_success = false;
        beast::error_code handshake_ec;

        ws_.async_handshake(address_.host, address_.path, [&](const beast::error_code& error) {
            handshake_ec = error;
            if (!error) handshake_success = true;
        });

        context_.restart();
        context_.run_for(timeout);

        if (!handshake_success)
        {
            ws_.next_layer().close(ec);
            std::string msg = handshake_ec ? handshake_ec.message() : "Handshake timed out";
            return {.value = false, .err = error::handshake_failed, .message = std::move(msg)};
        }

        ws_.binary(true);
        connected_ = true;
        return {.value = true, .err = error::none, .message = {}};
    }
    catch (const std::exception& ex)
    {
        return {.value = false, .err = error::connect_failed, .message = ex.what()};
    }
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
    beast::error_code ec;
    if (ws_.is_open())
    {
        ws_.close(websocket_beast::close_code::normal, ec);
    }
    if (ws_.next_layer().is_open())
    {
        ws_.next_layer().close(ec);
    }
    connected_ = false;
    return {.value = true, .err = error::none, .message = {}};
}

bool webSocket::is_open() const noexcept
{
    return connected_ && ws_.is_open();
}