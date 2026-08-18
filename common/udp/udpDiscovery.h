#ifndef SKETCHSYNC_UDP_DISCOVERY_H
#define SKETCHSYNC_UDP_DISCOVERY_H

#include <cstdint>
#include <string>
#include <vector>
#include <span>
#include <functional>
#include <optional>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include "common/results.h"
#include "common/network_constants.h"

namespace net = boost::asio;
using udp = net::ip::udp;

namespace udp_proto {

    namespace Opcode {
        constexpr uint8_t DISCOVER = 0x01;
        constexpr uint8_t OFFER    = 0x02;
    }

    struct Header {
        static constexpr size_t MAGIC_SIZE = 6;
        static constexpr char MAGIC[MAGIC_SIZE] = {'S', 'K', 'S', 'Y', 'N', 'C'};
        static constexpr size_t SIZE = MAGIC_SIZE + 1; // 7 bytes

        uint8_t opcode = 0;
    };

    struct Packet {
        Header header;
        std::vector<uint8_t> payload;

        [[nodiscard]] size_t getSize() const noexcept { return Header::SIZE + payload.size(); }
    };

    result<Header> parseHeader(std::span<const uint8_t> data);
    std::vector<uint8_t> serializePacket(const Packet& packet);

    struct DiscoverMessage {
        uint32_t session_id = 0;
    };

    std::vector<uint8_t> serializeDiscoverMessage(const DiscoverMessage& msg);
    result<DiscoverMessage> parseDiscoverMessage(std::span<const uint8_t> data);

    struct OfferMessage {
        uint32_t session_id = 0;
        uint16_t tcp_port = 0;
    };

    std::vector<uint8_t> serializeOfferMessage(const OfferMessage& msg);
    result<OfferMessage> parseOfferMessage(std::span<const uint8_t> data);

} // namespace udp_proto

namespace udp_discovery {

    constexpr unsigned short DEFAULT_UDP_PORT = net_config::DEFAULT_UDP_PORT;

    // Client function: broadcasts request for session_id and waits for offer.
    // Returns pair of (host_ip_string, tcp_port).
    result<std::pair<std::string, uint16_t>> discover_host(
        uint32_t session_id,
        std::chrono::milliseconds timeout = net_config::DEFAULT_DISCOVERY_TIMEOUT,
        unsigned short udp_port = net_config::DEFAULT_UDP_PORT
    );

    // Server/Host responder: listens on udp_port, replies to discover requests for active sessions.
    class responder {
    public:
        using lookup_callback = std::function<std::optional<uint16_t>(uint32_t session_id)>;

        responder(net::io_context& io, unsigned short udp_port, lookup_callback lookup);
        ~responder();

        responder(const responder&) = delete;
        responder& operator=(const responder&) = delete;

        void start();
        void stop();

    private:
        void run_loop();

        net::io_context& io_;
        unsigned short udp_port_;
        lookup_callback lookup_;
        std::unique_ptr<udp::socket> socket_;
        std::thread worker_;
        std::atomic<bool> stop_flag_{false};
    };

} // namespace udp_discovery

#endif // SKETCHSYNC_UDP_DISCOVERY_H
