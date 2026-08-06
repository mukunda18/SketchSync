#include "common/tcp/tcpServer.h"

tcpServer::tcpServer(net::io_context& context, const unsigned short port)
    : acceptor_(context, tcp::endpoint(tcp::v4(), port))
{}

tcpServer::~tcpServer()
{
    close();
}

result<tcp::socket> tcpServer::accept()
{
    boost::system::error_code ec;

    tcp::socket peer = acceptor_.accept(ec);

    if (ec)
    {
        return {
            .value = tcp::socket(acceptor_.get_executor()),
            .err = error::accept_failed,
            .message = ec.message()
        };
    }

    return {
        .value = std::move(peer),
        .err = error::none,
        .message = {}
    };
}

result<bool> tcpServer::close()
{
    if (!acceptor_.is_open())
        return {.value = true, .err = error::none, .message = {}};

    boost::system::error_code ec = acceptor_.close(ec);

    if (ec)
        return {.value = false, .err = error::close_failed, .message = ec.message()};

    return {.value = true, .err = error::none, .message = {}};
}

bool tcpServer::is_open() const noexcept
{
    return acceptor_.is_open();
}