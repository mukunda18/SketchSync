#include "engine/process/server_process.h"

#define NOMINMAX
#define NOGDI
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

server_process::~server_process()
{
    stop();
}

std::string server_process::quote_argument(const std::string& argument)
{
    if (argument.find_first_of(" \t\"") == std::string::npos)
        return argument;

    std::string quoted = "\"";
    for (const char ch : argument)
    {
        if (ch == '"')
            quoted += '\\';
        quoted += ch;
    }
    quoted += '"';
    return quoted;
}

std::string server_process::build_command_line(const std::filesystem::path& executable,
                                               const std::vector<std::string>& arguments)
{
    std::string command_line = quote_argument(executable.string());
    for (const auto& arg : arguments)
    {
        command_line += ' ';
        command_line += quote_argument(arg);
    }
    return command_line;
}

result<bool> server_process::start(const server_process_launch& launch)
{
    if (running_)
        return {.value = false, .err = error::rejected, .message = "server already running"};

    if (launch.executable.empty() || !std::filesystem::exists(launch.executable))
        return {.value = false, .err = error::rejected, .message = "server executable not found"};

    std::string command_line = build_command_line(launch.executable, launch.arguments);

    STARTUPINFOA startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};

    const BOOL created = CreateProcessA(
        launch.executable.string().c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup_info,
        &process_info);

    if (!created)
        return {.value = false, .err = error::connect_failed, .message = "failed to start server process"};

    process_handle_ = process_info.hProcess;
    thread_handle_ = process_info.hThread;
    running_ = true;
    return {.value = true, .err = error::none, .message = {}};
}

result<bool> server_process::stop(const unsigned exit_code)
{
    if (!running_)
        return {.value = true, .err = error::none, .message = {}};

    if (process_handle_ != nullptr)
        TerminateProcess(static_cast<HANDLE>(process_handle_), exit_code);

    if (thread_handle_ != nullptr)
        CloseHandle(static_cast<HANDLE>(thread_handle_));

    if (process_handle_ != nullptr)
        CloseHandle(static_cast<HANDLE>(process_handle_));

    process_handle_ = nullptr;
    thread_handle_ = nullptr;
    running_ = false;
    return {.value = true, .err = error::none, .message = {}};
}

bool server_process::running() const noexcept
{
    if (!running_ || process_handle_ == nullptr)
        return false;

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(static_cast<HANDLE>(process_handle_), &exit_code))
        return false;

    return exit_code == STILL_ACTIVE;
}
