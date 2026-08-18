#include "common/udp/udpDiscovery.h"
#include "common/bytes.h"
#include <boost/asio/steady_timer.hpp>
#include <cstring>
#include <iostream>

namespace udp_proto {

    result<Header> parseHeader(const std::span<const uint8_t> data) {
        if (data.size() < Header::SIZE) {
            return {.err = error::malformed, .message = "Packet too short for UDP header"};
        }
        if (std::memcmp(data.data(), Header::MAGIC, Header::MAGIC_SIZE) != 0) {
            return {.err = error::malformed, .message = "Invalid UDP magic header"};
        }
        size_t off = Header::MAGIC_SIZE;
        Header header;
        header.opcode = bytes::read8(data, off);
        return {.value = header, .err = error::none, .message = {}};
    }

    std::vector<uint8_t> serializePacket(const Packet& packet) {
        std::vector<uint8_t> buf(Header::SIZE + packet.payload.size());
        std::memcpy(buf.data(), Header::MAGIC, Header::MAGIC_SIZE);
        size_t off = Header::MAGIC_SIZE;
        bytes::write8(buf, off, packet.header.opcode);
        if (!packet.payload.empty()) {
            std::memcpy(buf.data() + off, packet.payload.data(), packet.payload.size());
        }
        return buf;
    }

    std::vector<uint8_t> serializeDiscoverMessage(const DiscoverMessage& msg) {
        std::vector<uint8_t> payload(sizeof(uint32_t));
        size_t off = 0;
        bytes::write32(payload, off, msg.session_id);
        const Packet packet{
            .header = Header{.opcode = Opcode::DISCOVER},
            .payload = std::move(payload)
        };
        return serializePacket(packet);
    }

    result<DiscoverMessage> parseDiscoverMessage(const std::span<const uint8_t> data) {
        const auto header_res = parseHeader(data);
        if (!header_res) return {.err = header_res.err, .message = header_res.message};
        if (header_res.value.opcode != Opcode::DISCOVER) {
            return {.err = error::malformed, .message = "Not a DISCOVER opcode"};
        }
        if (data.size() < Header::SIZE + sizeof(uint32_t)) {
            return {.err = error::malformed, .message = "DISCOVER payload too short"};
        }
        size_t off = Header::SIZE;
        const uint32_t session_id = bytes::read32(data, off);
        return {.value = DiscoverMessage{.session_id = session_id}, .err = error::none, .message = {}};
    }

    std::vector<uint8_t> serializeOfferMessage(const OfferMessage& msg) {
        std::vector<uint8_t> payload(sizeof(uint32_t) + sizeof(uint16_t));
        size_t off = 0;
        bytes::write32(payload, off, msg.session_id);
        bytes::write16(payload, off, msg.tcp_port);
        const Packet packet{
            .header = Header{.opcode = Opcode::OFFER},
            .payload = std::move(payload)
        };
        return serializePacket(packet);
    }

    result<OfferMessage> parseOfferMessage(const std::span<const uint8_t> data) {
        const auto header_res = parseHeader(data);
        if (!header_res) return {.err = header_res.err, .message = header_res.message};
        if (header_res.value.opcode != Opcode::OFFER) {
            return {.err = error::malformed, .message = "Not an OFFER opcode"};
        }
        if (data.size() < Header::SIZE + sizeof(uint32_t) + sizeof(uint16_t)) {
            return {.err = error::malformed, .message = "OFFER payload too short"};
        }
        size_t off = Header::SIZE;
        const uint32_t session_id = bytes::read32(data, off);
        const uint16_t tcp_port = bytes::read16(data, off);
        return {.value = OfferMessage{.session_id = session_id, .tcp_port = tcp_port}, .err = error::none, .message = {}};
    }

} // namespace udp_proto

namespace udp_discovery {

    result<std::pair<std::string, uint16_t>> discover_host(
        const uint32_t session_id,
        const std::chrono::milliseconds timeout,
        const unsigned short udp_port)
    {
        try {
            net::io_context io;
            udp::socket socket(io);
            boost::system::error_code ec;

            socket.open(udp::v4(), ec);
            if (ec) {
                return {.err = error::connect_failed, .message = "Failed to open UDP socket: " + ec.message()};
            }

            socket.set_option(boost::asio::socket_base::broadcast(true), ec);
            socket.set_option(boost::asio::socket_base::reuse_address(true), ec);

            socket.bind(udp::endpoint(udp::v4(), 0), ec);
            if (ec) {
                return {.err = error::connect_failed, .message = "Failed to bind UDP client: " + ec.message()};
            }

            const auto req_data = udp_proto::serializeDiscoverMessage({.session_id = session_id});

            // Send broadcast on LAN
            udp::endpoint bcast_ep(boost::asio::ip::address_v4::broadcast(), udp_port);
            socket.send_to(net::buffer(req_data), bcast_ep, 0, ec);

            // Also send directly to localhost loopback for same-machine testing
            udp::endpoint loopback_ep(boost::asio::ip::address_v4::loopback(), udp_port);
            socket.send_to(net::buffer(req_data), loopback_ep, 0, ec);

            std::array<uint8_t, 256> recv_buf{};
            udp::endpoint sender_ep;
            bool found = false;
            std::string host_ip;
            uint16_t host_port = 0;

            net::steady_timer timer(io);
            timer.expires_after(timeout);

            std::function<void(const boost::system::error_code&, size_t)> do_receive;
            do_receive = [&](const boost::system::error_code& recv_ec, const size_t bytes) {
                if (recv_ec || found) {
                    return;
                }
                const auto offer_res = udp_proto::parseOfferMessage(std::span<const uint8_t>(recv_buf.data(), bytes));
                if (offer_res && offer_res.value.session_id == session_id) {
                    found = true;
                    host_ip = sender_ep.address().to_string();
                    if (sender_ep.address().is_unspecified() || sender_ep.address().is_loopback()) {
                        host_ip = "127.0.0.1";
                    }
                    host_port = offer_res.value.tcp_port;
                    timer.cancel();
                    socket.close();
                    return;
                }
                // Continue listening until timer cancels
                if (!found && socket.is_open()) {
                    socket.async_receive_from(net::buffer(recv_buf), sender_ep, do_receive);
                }
            };

            socket.async_receive_from(net::buffer(recv_buf), sender_ep, do_receive);

            timer.async_wait([&](const boost::system::error_code& timer_ec) {
                if (!timer_ec && !found) {
                    boost::system::error_code close_ec;
                    socket.close(close_ec);
                }
            });

            io.run();

            if (found) {
                return {.value = {host_ip, host_port}, .err = error::none, .message = {}};
            }

            return {.err = error::connect_failed, .message = "Session #" + std::to_string(session_id) + " not found on network"};
        } catch (const std::exception& ex) {
            return {.err = error::connect_failed, .message = ex.what()};
        }
    }

    responder::responder(net::io_context& io, const unsigned short udp_port, lookup_callback lookup)
        : io_(io), udp_port_(udp_port), lookup_(std::move(lookup))
    {
    }

    responder::~responder() {
        stop();
    }

    void responder::start() {
        if (socket_ && socket_->is_open()) return;

        stop_flag_.store(false);
        try {
            socket_ = std::make_unique<udp::socket>(io_);
            const udp::endpoint listen_ep(udp::v4(), udp_port_);
            boost::system::error_code ec;

            socket_->open(listen_ep.protocol(), ec);
            if (ec) return;

            socket_->set_option(boost::asio::socket_base::reuse_address(true), ec);
            socket_->set_option(boost::asio::socket_base::broadcast(true), ec);

            socket_->bind(listen_ep, ec);
            if (ec) {
                socket_->close(ec);
                return;
            }

            worker_ = std::thread(&responder::run_loop, this);
        } catch (...) {
            // Ignore startup failures gracefully
        }
    }

    void responder::stop() {
        stop_flag_.store(true);
        if (socket_) {
            boost::system::error_code ec;
            socket_->close(ec);
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        socket_.reset();
    }

    void responder::run_loop() {
        std::array<uint8_t, 256> recv_buf{};
        while (!stop_flag_.load() && socket_ && socket_->is_open()) {
            udp::endpoint sender_ep;
            boost::system::error_code ec;
            const size_t bytes = socket_->receive_from(net::buffer(recv_buf), sender_ep, 0, ec);
            if (ec) {
                if (stop_flag_.load() || !socket_->is_open()) break;
                continue;
            }

            const auto req = udp_proto::parseDiscoverMessage(std::span<const uint8_t>(recv_buf.data(), bytes));
            if (!req) continue;

            if (lookup_) {
                if (const auto tcp_p = lookup_(req.value.session_id); tcp_p.has_value()) {
                    const auto offer_bytes = udp_proto::serializeOfferMessage({
                        .session_id = req.value.session_id,
                        .tcp_port = *tcp_p
                    });
                    socket_->send_to(net::buffer(offer_bytes), sender_ep, 0, ec);
                }
            }
        }
    }

} // namespace udp_discovery
