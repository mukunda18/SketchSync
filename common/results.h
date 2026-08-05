#ifndef RESULT_H
#define RESULT_H

#include <string>

enum class error
{
    none = 0,
    resolve_failed,
    connect_failed,
    send_failed,
    receive_failed,
    closed,
    malformed,
    handshake_failed,
};

template <typename T>
struct result
{
    T value;
    error error = error::none;
    std::string message;

    explicit operator bool() const { return error == error::none; }
};

#endif
