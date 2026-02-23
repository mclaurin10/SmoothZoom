#pragma once
// =============================================================================
// SmoothZoom — SeqLock
// Sequence-lock for small structs (e.g., ScreenRect).
// Writer is infrequent (UIA thread), reader is frequent (render thread).
// Reader retries on concurrent write — no mutex contention on hot path.
// Doc 3 §2.4
// =============================================================================

#include <atomic>
#include <cstring>

namespace SmoothZoom
{

template <typename T>
class SeqLock
{
public:
    void write(const T& value)
    {
        sequence_.fetch_add(1, std::memory_order_release); // odd = write in progress
        data_ = value;
        sequence_.fetch_add(1, std::memory_order_release); // even = write complete
    }

    T read() const
    {
        T result;
        uint32_t seq0, seq1;
        do
        {
            seq0 = sequence_.load(std::memory_order_acquire);
            result = data_;
            seq1 = sequence_.load(std::memory_order_acquire);
        } while (seq0 != seq1 || (seq0 & 1));
        return result;
    }

private:
    std::atomic<uint32_t> sequence_{0};
    T data_{};
};

} // namespace SmoothZoom
