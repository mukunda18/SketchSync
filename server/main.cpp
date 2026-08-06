#include "server/server.h"
#include <boost/asio/io_context.hpp>
#include <iostream>
#include <string>

int main()
{
    try
    {
        net::io_context io;
        server srv(io, 8080, 9000);
        srv.run();

        std::cout << "SketchSync server started. Press Enter to stop...\n";
        std::string line;
        std::getline(std::cin, line);

        srv.shutdown();
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Server error: " << ex.what() << '\n';
        return 1;
    }
}
