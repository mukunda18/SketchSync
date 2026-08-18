#ifndef SKETCHSYNC_NETWORK_CONSTANTS_H
#define SKETCHSYNC_NETWORK_CONSTANTS_H

#include <cstdint>
#include <string_view>
#include <chrono>

namespace net_config
{
    // Standard Port Definitions
    constexpr unsigned short DEFAULT_WS_PORT = 8080;
    constexpr unsigned short DEFAULT_TCP_PORT = 9000;
    constexpr unsigned short DEFAULT_UDP_PORT = 9002;

    // String Port Representations for Resolvers / UI Defaults
    constexpr std::string_view DEFAULT_WS_PORT_STR = "8080";
    constexpr std::string_view DEFAULT_TCP_PORT_STR = "9000";
    constexpr std::string_view DEFAULT_UDP_PORT_STR = "9002";

    // Default Host
    constexpr std::string_view DEFAULT_HOST = "127.0.0.1";

    // Standard Timeouts
    constexpr auto DEFAULT_CONNECT_TIMEOUT = std::chrono::milliseconds(3000);
    constexpr auto DEFAULT_DISCOVERY_TIMEOUT = std::chrono::milliseconds(3000);
}

#endif // SKETCHSYNC_NETWORK_CONSTANTS_H
