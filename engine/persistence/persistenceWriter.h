#ifndef SKETCHSYNC_PERSISTENCE_WRITER_H
#define SKETCHSYNC_PERSISTENCE_WRITER_H

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "common/canvas/draw_operation.h"

struct persistence_writer
{
    explicit persistence_writer(std::string file_path, uint32_t start_after_seq = 0);

    persistence_writer(const persistence_writer&) = delete;
    persistence_writer& operator=(const persistence_writer&) = delete;
    persistence_writer(persistence_writer&&) = delete;
    persistence_writer& operator=(persistence_writer&&) = delete;

    ~persistence_writer();

    void enqueue(const draw_operation& op);
    [[nodiscard]] bool     healthy()    const noexcept { return !failed_.load(); }
    [[nodiscard]] uint32_t saved_seq()  const noexcept { return saved_seq_.load(); }

private:
    void run();
    void update_header_seq(std::fstream& file);

    std::string             file_path_;
    uint32_t                start_after_seq_;
    std::atomic<uint32_t>   saved_seq_{0};
    std::queue<draw_operation> queue_;
    std::mutex              queue_mutex_;
    std::condition_variable cv_;
    std::atomic<bool>       stop_{false};
    std::atomic<bool>       failed_{false};
    std::thread             thread_;
};

#endif
