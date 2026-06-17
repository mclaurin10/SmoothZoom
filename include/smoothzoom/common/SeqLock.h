#pragma once
// =============================================================================
// SmoothZoom — SeqLock
// Sequence-lock for small structs (e.g., ScreenRect).
// Writer is infrequent (UIA thread), reader is frequent (render thread).
// Reader retries on concurrent write — no mutex contention on hot path.
// Doc 3 §2.4
//
// Race-free in the C++ memory model: the payload is stored as an array of
// std::atomic<unsigned char>, so every concurrent touch of it is an atomic
// access (no data race under [intro.races]). The sequence counter still lets
// the reader discard inconsistent (torn) snapshots and retry. Memory ordering
// follows the Boehm "Can Seqlocks Get Along with Programming Language Memory
// Models?" formulation: relaxed payload accesses, release seq bumps in the
// writer, and an acquire fence in the reader placed AFTER the payload loads
// and BEFORE the sequence re-read. This is portable (no reliance on x86-64
// TSO) and lock-free — payload copies use stack locals only, so it stays
// within the render-thread hot-path invariants (no heap, no mutex).
// =============================================================================

#include <atomic>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace SmoothZoom
{

template <typename T>
class SeqLock
{
    static_assert(std::is_trivially_copyable_v<T>,
                  "SeqLock<T> requires a trivially copyable T");

public:
    void write(const T& value)
    {
        unsigned char buf[sizeof(T)];
        std::memcpy(buf, &value, sizeof(T));                // stack local
        sequence_.fetch_add(1, std::memory_order_release);  // odd = write in progress
        // Keep the payload stores from being reordered before the odd-seq publication.
        std::atomic_thread_fence(std::memory_order_release);
        for (std::size_t i = 0; i < sizeof(T); ++i)
            data_[i].store(buf[i], std::memory_order_relaxed);
        sequence_.fetch_add(1, std::memory_order_release);  // even = write complete
    }

    T read() const
    {
        unsigned char buf[sizeof(T)];
        uint32_t seq0, seq1;
        do
        {
            seq0 = sequence_.load(std::memory_order_acquire);
            for (std::size_t i = 0; i < sizeof(T); ++i)
                buf[i] = data_[i].load(std::memory_order_relaxed);
            // Acquire fence AFTER payload loads, BEFORE the sequence re-read:
            // prevents seq1 from being observed before the payload bytes.
            std::atomic_thread_fence(std::memory_order_acquire);
            seq1 = sequence_.load(std::memory_order_relaxed);
        } while (seq0 != seq1 || (seq0 & 1u));
        T result;
        std::memcpy(&result, buf, sizeof(T));               // stack local
        return result;
    }

private:
    std::atomic<uint32_t> sequence_{0};
    std::atomic<unsigned char> data_[sizeof(T)] = {};       // value-init each byte to 0
};

} // namespace SmoothZoom
