#ifndef RESULT_H
#define RESULT_H

enum class ws_error
{
    none = 0,
    resolve_failed,
    connect_failed,
    handshake_failed,
    send_failed,
    receive_failed,
    closed,
};

template <typename T>
struct result
{
    T value;
    ws_error error = ws_error::none;
    std::string message;

    explicit operator bool() const { return error == ws_error::none; }
};

#endif