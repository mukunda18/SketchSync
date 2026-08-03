#include "websocket.h"

#include <iostream>
#include <utility>


websocket::websocket():
    resolver(context),
    ws(context)
{
    ws.binary(true);
}

websocket::websocket(webaddr address):
    address(std::move(address)),
    resolver(context),
    ws(context)
{
    ws.binary(true);
}

void websocket::connect()
{
    try
    {
        const auto results = resolver.resolve(
            address.host,
            address.port
        );

        net::connect(
            ws.next_layer(),
            results
        );

        ws.handshake(
            address.host,
            address.path
        );

        std::cout
            << "Connected to "
            << address.host
            << ":"
            << address.port
            << std::endl;
    }
    catch(const std::exception& e)
    {
        std::cerr
            << "Connection failed: "
            << e.what()
            << std::endl;
    }

}

void websocket::send(const std::vector<uint8_t>& data)
{
    ws.binary(true);
}

std::string websocket::receive()
{
    try
    {
        beast::flat_buffer buffer;
        ws.read(buffer);
        return beast::buffers_to_string(
            buffer.data()
        );
    }

    catch(const std::exception& e)
    {
        std::cerr
            << "Receive failed: "
            << e.what()
            << std::endl;
        return {};
    }

}

void websocket::close()
{
    try{
        ws.close(
            websocket_beast::close_code::normal
        );
    }
    catch(const std::exception& e)
    {
        std::cerr
            << "Close failed: "
            << e.what()
            << std::endl;
    }
}