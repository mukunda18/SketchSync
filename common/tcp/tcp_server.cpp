#include "tcp_server.h"

tcp_server::tcp_server(net::io_context& context, const unsigned short port)
    : acceptor_(context, tcp::endpoint(tcp::v4(), port))
{}

tcp_server::~tcp_server()
{
    close();
}

result<tcp::socket> tcp_server::accept()
{
    boost::system::error_code ec;

    tcp::socket peer = acceptor_.accept(ec);

    if (ec)
    {
        return {
            .value = tcp::socket(acceptor_.get_executor()),
            .error = error::accept_failed,
            .message = ec.message()
        };
    }

    return {
        .value = std::move(peer),
        .error = error::none,
        .message = {}
    };
}

result<bool> tcp_server::close()
{
    if (!acceptor_.is_open())
        return {.value = true, .error = error::none, .message = {}};

    boost::system::error_code ec = acceptor_.close(ec);

    if (ec)
        return {.value = false, .error = error::close_failed, .message = ec.message()};

    return {.value = true, .error = error::none, .message = {}};
}

bool tcp_server::is_open() const noexcept
{
    return acceptor_.is_open();
}