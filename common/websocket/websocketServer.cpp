#include "webSocketServer.h"

webSocketServer::webSocketServer(net::io_context& context, unsigned short port)
    : acceptor_(context, tcp::endpoint(tcp::v4(), port))
{}

webSocketServer::~webSocketServer()
{
    if (acceptor_.is_open())
        close();
}

result<webSocket_beast::stream<tcp::socket>> webSocketServer::accept()
{
    beast::error_code ec;

    tcp::socket peer = acceptor_.accept(ec);
    if (ec)
    {
        return {
            .value = webSocket_beast::stream<tcp::socket>(acceptor_.get_executor()),
            .err = error::accept_failed,
            .message = ec.message()
        };
    }

    webSocket_beast::stream<tcp::socket> ws(std::move(peer));
    ws.binary(true);

    ws.accept(ec);
    if (ec)
    {
        return {
            .value = webSocket_beast::stream<tcp::socket>(acceptor_.get_executor()),
            .err = error::handshake_failed,
            .message = ec.message()
        };
    }

    return {.value = std::move(ws), .err = error::none, .message = {}};
}

result<bool> webSocketServer::close()
{
    if (!acceptor_.is_open())
        return {.value = true, .err = error::none, .message = {}};

    beast::error_code ec = acceptor_.close(ec);

    if (ec)
        return {.value = false, .err = error::close_failed, .message = ec.message()};

    return {.value = true, .err = error::none, .message = {}};
}

bool webSocketServer::is_open() const noexcept
{
    return acceptor_.is_open();
}