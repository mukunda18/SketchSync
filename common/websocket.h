#ifndef SKETCHSYNC_WEBSOCKET_H
#define SKETCHSYNC_WEBSOCKET_H

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket_beast = beast::websocket;

using string = std::string;
using tcp = net::ip::tcp;

struct webaddr
{
    string address;
    string port;
};

struct websocket
{

    webaddr address;
    net::io_context context;
    tcp::resolver resolver;
    tcp::socket socket;

    websocket();
    explicit websocket(const webaddr& address);
    void bind(const webaddr& address);
};



#endif //SKETCHSYNC_WEBSOCKET_H
