#include <iostream>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>


namespace beast =
    boost::beast;

namespace websocket =
    beast::websocket;

namespace net =
    boost::asio;

using tcp =
    net::ip::tcp;



int main()
{
    try
    {
        net::io_context ioc;


        // Resolver
        tcp::resolver resolver(ioc);


        auto results =
            resolver.resolve(
                "echo.websocket.events",
                "80"
            );


        // TCP socket
        tcp::socket socket(ioc);


        net::connect(
            socket,
            results
        );


        // WebSocket stream on top of TCP
        websocket::stream<tcp::socket> ws(
            std::move(socket)
        );


        // WebSocket handshake
        ws.handshake(
            "echo.websocket.events",
            "/"
        );


        std::cout
            << "Connected\n";


        // Send message

        ws.write(
            net::buffer(
                "Hello WebSocket"
            )
        );


        // Receive

        beast::flat_buffer buffer;


        ws.read(buffer);



        std::cout
            << "Received: "
            << beast::make_printable(
                   buffer.data()
               )
            << "\n";



        // Close WebSocket

        ws.close(
            websocket::close_code::normal
        );


    }
    catch(std::exception& e)
    {
        std::cout
            << "Error: "
            << e.what()
            << "\n";
    }
    int a;
    std::cin >> a;
}