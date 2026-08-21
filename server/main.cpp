#include "server/server.h"
#include "common/network_constants.h"
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace
{
    unsigned short parse_port(const char* value, const unsigned short fallback)
    {
        if (!value)
            return fallback;

        try
        {
            const int parsed = std::stoi(value);
            if (parsed < 0 || parsed > 65535)
                return fallback;
            return static_cast<unsigned short>(parsed);
        }
        catch (...)
        {
            return fallback;
        }
    }
}

int main(const int argc, char* argv[])
{
    try
    {
        unsigned short ws_port = net_config::DEFAULT_WS_PORT;
        unsigned short tcp_port = net_config::DEFAULT_TCP_PORT;
        unsigned short udp_port = net_config::DEFAULT_UDP_PORT;

        for (int i = 1; i < argc; ++i)
        {
            if (const std::string arg = argv[i]; arg == "--ws-port" && i + 1 < argc)
                ws_port = parse_port(argv[++i], ws_port);
            else if (arg == "--tcp-port" && i + 1 < argc)
                tcp_port = parse_port(argv[++i], tcp_port);
            else if (arg == "--udp-port" && i + 1 < argc)
                udp_port = parse_port(argv[++i], udp_port);
        }

        net::io_context io;
        server srv(io, ws_port, tcp_port, udp_port);
        srv.run();

        std::cout << "SketchSync server started.\n";
        while (true)
            std::this_thread::sleep_for(std::chrono::hours(24));

        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Server error: " << ex.what() << '\n';
        return 1;
    }
}
