#include "engine/persistence/persistenceWriter.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <utility>

#include "common/bytes.h"

persistence_writer::persistence_writer(std::string file_path, const uint32_t start_after_seq)
    : file_path_(std::move(file_path)),
      start_after_seq_(start_after_seq),
      saved_seq_(start_after_seq)
{
    thread_ = std::thread(&persistence_writer::run, this);
}

void persistence_writer::stop()
{
    stop_.store(true);
    cv_.notify_one();
    if (thread_.joinable())
        thread_.join();
}

persistence_writer::~persistence_writer()
{
    stop();
}

void persistence_writer::enqueue(const draw_operation& op)
{
    {
        std::lock_guard lock(queue_mutex_);
        queue_.push(op);
    }
    cv_.notify_one();
}

void persistence_writer::update_header_seq(std::fstream& file) const
{
    const uint32_t seq = saved_seq_.load();
    std::vector<uint8_t> buffer(4);
    size_t off = 0;
    bytes::write32(buffer, off, seq);
    file.seekp(8, std::ios::beg);
    file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
}

void persistence_writer::run()
{
    // Open file in binary read/write mode. If new, open with app to create, then re-open as in/out/binary
    if (!std::filesystem::exists(file_path_) || std::filesystem::file_size(file_path_) == 0)
    {
        if (std::ofstream create_file(file_path_, std::ios::binary); create_file.is_open())
        {
            constexpr std::array<uint8_t, 4> MAGIC{'S', 'K', 'S', 'Y'};
            create_file.write(reinterpret_cast<const char*>(MAGIC.data()), MAGIC.size());
            constexpr std::array<uint8_t, 4> VERSION{0, 0, 0, 1}; // version 1
            create_file.write(reinterpret_cast<const char*>(VERSION.data()), VERSION.size());
            
            std::vector<uint8_t> seq_buf(4);
            size_t off = 0;
            bytes::write32(seq_buf, off, start_after_seq_);
            create_file.write(reinterpret_cast<const char*>(seq_buf.data()), seq_buf.size());
            
            constexpr std::array<uint8_t, 4> RESERVED{0, 0, 0, 0};
            create_file.write(reinterpret_cast<const char*>(RESERVED.data()), RESERVED.size());
        }
    }

    std::fstream file(file_path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        failed_.store(true);
        return;
    }

    while (true)
    {
        std::unique_lock lock(queue_mutex_);
        cv_.wait(lock, [this] { return !queue_.empty() || stop_.load(); });

        bool wrote_any = false;
        while (!queue_.empty())
        {
            draw_operation op = std::move(queue_.front());
            queue_.pop();
            lock.unlock();

            // Only write ops that have seq > start_after_seq_ (and skip temporary draft/ACK-less ops with seq == 0 unless they are local host draws, but host canvas.apply assigns seq > 0)
            if (op.seq > start_after_seq_)
            {
                // Ensure we are at the end of the file for appending records
                file.seekp(0, std::ios::end);

                const auto payload = serializeDrawOperation(op);
                std::vector<uint8_t> length_prefix(4);
                size_t off = 0;
                bytes::write32(length_prefix, off, static_cast<uint32_t>(payload.size()));
                file.write(reinterpret_cast<const char*>(length_prefix.data()), static_cast<std::streamsize>(length_prefix.size()));
                file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));

                if (op.seq > saved_seq_.load())
                {
                    saved_seq_.store(op.seq);
                }
                wrote_any = true;
            }

            if (!file)
            {
                failed_.store(true);
                lock.lock();
                while (!queue_.empty())
                    queue_.pop();
                break;
            }

            lock.lock();
        }

        lock.unlock();
        if (wrote_any && file)
        {
            update_header_seq(file);
            file.flush();
        }
        if (!file)
            failed_.store(true);
        lock.lock();

        if (stop_.load())
            break;
    }

    if (file)
    {
        update_header_seq(file);
        file.flush();
    }
}
