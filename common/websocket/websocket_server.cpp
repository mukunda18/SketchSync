#include "websocket_server.h"

websocket_server::websocket_server(net::io_context& context, unsigned short port)
    : acceptor_(context, tcp::endpoint(tcp::v4(), port))
{}

websocket_server::~websocket_server()
{
    if (acceptor_.is_open())
        close();
}

result<websocket_beast::stream<tcp::socket>> websocket_server::accept()
{
    beast::error_code ec;

    tcp::socket peer = acceptor_.accept(ec);
    if (ec)
    {
        return {
            .value = websocket_beast::stream<tcp::socket>(acceptor_.get_executor()),
            .error = error::accept_failed,
            .message = ec.message()
        };
    }

    websocket_beast::stream<tcp::socket> ws(std::move(peer));
    ws.binary(true);

    ws.accept(ec);
    if (ec)
    {
        return {
            .value = websocket_beast::stream<tcp::socket>(acceptor_.get_executor()),
            .error = error::handshake_failed,
            .message = ec.message()
        };
    }

    return {.value = std::move(ws), .error = error::none, .message = {}};
}

result<bool> websocket_server::close()
{
    if (!acceptor_.is_open())
        return {.value = true, .error = error::none, .message = {}};

    beast::error_code ec = acceptor_.close(ec);

    if (ec)
        return {.value = false, .error = error::close_failed, .message = ec.message()};

    return {.value = true, .error = error::none, .message = {}};
}

bool websocket_server::is_open() const noexcept
{
    return acceptor_.is_open();
}