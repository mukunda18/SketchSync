#ifndef SKETCHSYNC_BYTES_H
#define SKETCHSYNC_BYTES_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

namespace bytes
{
    using std::size_t;

    inline uint8_t read8(std::span<const uint8_t> data, size_t& off)
    {
        return data[off++];
    }

    inline uint16_t read16(std::span<const uint8_t> data, size_t& off)
    {
        const uint16_t value = (static_cast<uint16_t>(data[off]) << 8) |
                               static_cast<uint16_t>(data[off + 1]);
        off += 2;
        return value;
    }

    inline uint32_t read32(std::span<const uint8_t> data, size_t& off)
    {
        const uint32_t value = (static_cast<uint32_t>(data[off]) << 24) |
                               (static_cast<uint32_t>(data[off + 1]) << 16) |
                               (static_cast<uint32_t>(data[off + 2]) << 8) |
                               static_cast<uint32_t>(data[off + 3]);
        off += 4;
        return value;
    }

    inline uint64_t read64(std::span<const uint8_t> data, size_t& off)
    {
        const uint64_t high = read32(data, off);
        const uint64_t low = read32(data, off);
        return (high << 32) | low;
    }

    inline void write8(std::vector<uint8_t>& buf, size_t& off, uint8_t v)
    {
        buf[off++] = v;
    }

    inline void write16(std::vector<uint8_t>& buf, size_t& off, uint16_t v)
    {
        buf[off++] = static_cast<uint8_t>(v >> 8);
        buf[off++] = static_cast<uint8_t>(v);
    }

    inline void write32(std::vector<uint8_t>& buf, size_t& off, uint32_t v)
    {
        buf[off++] = static_cast<uint8_t>(v >> 24);
        buf[off++] = static_cast<uint8_t>(v >> 16);
        buf[off++] = static_cast<uint8_t>(v >> 8);
        buf[off++] = static_cast<uint8_t>(v);
    }

    inline void write64(std::vector<uint8_t>& buf, size_t& off, uint64_t v)
    {
        write32(buf, off, static_cast<uint32_t>(v >> 32));
        write32(buf, off, static_cast<uint32_t>(v));
    }
}

#endif
