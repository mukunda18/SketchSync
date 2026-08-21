#ifndef SKETCHSYNC_SERVER_PROCESS_H
#define SKETCHSYNC_SERVER_PROCESS_H

#include <filesystem>
#include <string>
#include <vector>

#include "common/results.h"

struct server_process
{
    server_process() = default;

    server_process(const server_process&) = delete;
    server_process& operator=(const server_process&) = delete;
    server_process(server_process&&) = delete;
    server_process& operator=(server_process&&) = delete;

    result<bool> start(const std::filesystem::path& executable, const std::vector<std::string>& arguments);
    result<bool> stop(unsigned exit_code = 0);

    [[nodiscard]] bool running() const noexcept;

private:
    static std::string quote_argument(const std::string& argument);
    static std::string build_command_line(const std::filesystem::path& executable,
                                          const std::vector<std::string>& arguments);

    void* process_handle_ = nullptr;
    void* thread_handle_ = nullptr;
    void* job_handle_ = nullptr;
    bool running_ = false;
};

#endif
