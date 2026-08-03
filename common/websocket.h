#ifndef SKETCHSYNC_WEBSOCKET_H
#define SKETCHSYNC_WEBSOCKET_H

#include <string>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>


namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket_beast = beast::websocket;

using tcp = net::ip::tcp;


struct webaddr
{
    std::string host;
    std::string port;
    std::string path = "/";
};


struct websocket
{
    webaddr address;
    net::io_context context;
    tcp::resolver resolver;
    websocket_beast::stream<tcp::socket> ws;


    websocket();
    explicit websocket(webaddr address);

    void connect();
    void send(const std::vector<uint8_t>& data);
    std::string receive();
    void close();
};


#endif