#pragma once
// =============================================================================
// SmoothZoom — Lock-Free Queue
// SPSC (single-producer, single-consumer) queue for ZoomCommand.
// Producer: main thread (hook callbacks). Consumer: render thread.
// Doc 3 §2.4
// =============================================================================

#include <atomic>
#include <array>
#include <optional>

namespace SmoothZoom
{

template <typename T, size_t Capacity = 64>
class LockFreeQueue
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

public:
    bool push(const T& item)
    {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & (Capacity - 1);
        if (next == tail_.load(std::memory_order_acquire))
            return false; // full
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    std::optional<T> pop()
    {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return std::nullopt; // empty
        T item = buffer_[tail];
        tail_.store((tail + 1) & (Capacity - 1), std::memory_order_release);
        return item;
    }

private:
    std::array<T, Capacity> buffer_{};
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

} // namespace SmoothZoom
