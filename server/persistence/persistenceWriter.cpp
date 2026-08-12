#include "server/persistence/persistenceWriter.h"
#include "common/protocol/message.h"
#include <fstream>

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
        return;

    while (true)
    {
        std::unique_lock lock(queue_mutex_);
        cv_.wait(lock, [this] { return !queue_.empty() || stop_.load(); });

        while (!queue_.empty())
        {
            draw_operation op = std::move(queue_.front());
            queue_.pop();
            lock.unlock();

            // Write as a full protocol frame so the file can be replayed directly
            const auto payload = serializeDrawOperation(op);
            const Message frame{
                .header = Header{.opcode = Opcode::DRAW, .flags = 0, .length = static_cast<uint32_t>(payload.size())},
                .payload = payload
            };
            const auto buf = serializeMessage(frame);
            file.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
            file.flush();

            lock.lock();
        }

        if (stop_.load())
            break;
    }
}
