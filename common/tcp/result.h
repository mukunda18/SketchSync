#ifndef TCP_RESULT_H
#define TCP_RESULT_H

#include <string>

enum class tcp_error
{
    none = 0,
    resolve_failed,
    connect_failed,
    send_failed,
    receive_failed,
    closed,
    malformed,
};

template <typename T>
struct result
{
    T value;
    tcp_error error = tcp_error::none;
    std::string message;

    explicit operator bool() const { return error == tcp_error::none; }
};

#endif
