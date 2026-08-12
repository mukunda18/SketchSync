#include "engine/persistence/persistenceWriter.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <utility>

#include "common/bytes.h"

persistence_writer::persistence_writer(std::string file_path)
    : file_path_(std::move(file_path))
{
    thread_ = std::thread(&persistence_writer::run, this);
}

persistence_writer::~persistence_writer()
{
    stop_.store(true);
    cv_.notify_one();
    if (thread_.joinable())
        thread_.join();
}

void persistence_writer::enqueue(const draw_operation& op)
{
    {
        std::lock_guard lock(queue_mutex_);
        queue_.push(op);
    }
    cv_.notify_one();
}

void persistence_writer::run()
{
    std::ofstream file(file_path_, std::ios::binary | std::ios::app);
    if (!file.is_open())
    {
        failed_.store(true);
        return;
    }

    constexpr std::array<uint8_t, 4> MAGIC{'S', 'S', 'D', 'O'};
    if (!std::filesystem::exists(file_path_) || std::filesystem::file_size(file_path_) == 0)
        file.write(reinterpret_cast<const char*>(MAGIC.data()), MAGIC.size());

    while (true)
    {
        std::unique_lock lock(queue_mutex_);
        cv_.wait(lock, [this] { return !queue_.empty() || stop_.load(); });

        while (!queue_.empty())
        {
            draw_operation op = std::move(queue_.front());
            queue_.pop();
            lock.unlock();

            const auto payload = serializeDrawOperation(op);
            std::vector<uint8_t> length_prefix(4);
            size_t off = 0;
            bytes::write32(length_prefix, off, static_cast<uint32_t>(payload.size()));
            file.write(reinterpret_cast<const char*>(length_prefix.data()), static_cast<std::streamsize>(length_prefix.size()));
            file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));

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
        file.flush();
        if (!file)
            failed_.store(true);
        lock.lock();

        if (stop_.load())
            break;
    }

    file.flush();
}
