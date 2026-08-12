#ifndef SKETCHSYNC_CANVAS_H
#define SKETCHSYNC_CANVAS_H

#include <atomic>
#include <mutex>
#include <vector>
#include "common/canvas/draw_operation.h"

struct canvas
{
    std::atomic<uint32_t> next_seq{1};
    mutable std::mutex operations_mutex;
    std::vector<draw_operation> operations;

    // Stamps op with the next sequence number, appends it, and returns the assigned seq.
    uint32_t append(draw_operation op)
    {
        const uint32_t seq = next_seq.fetch_add(1);
        op.seq = seq;
        std::lock_guard lock(operations_mutex);
        operations.push_back(std::move(op));
        return seq;
    }

    std::vector<draw_operation> snapshot() const
    {
        std::lock_guard lock(operations_mutex);
        return operations;
    }
};

#endif
