// =============================================================================
// Unit tests — SeqLock
//
// Validates the race-free SeqLock: single-threaded round-trip plus a concurrent
// writer/reader torn-read check. The writer always publishes a rectangle whose
// four fields are equal ({n,n,n,n}); a torn read would expose mismatched fields,
// so the reader asserts the four fields stay equal on every read.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "smoothzoom/common/SeqLock.h"
#include "smoothzoom/common/Types.h"

#include <atomic>
#include <thread>

using namespace SmoothZoom;

TEST_CASE("SeqLock round-trips a value single-threaded", "[seqlock]")
{
    SeqLock<ScreenRect> lock;

    ScreenRect def = lock.read(); // default-constructed payload is all zero
    REQUIRE(def.left == 0);
    REQUIRE(def.top == 0);
    REQUIRE(def.right == 0);
    REQUIRE(def.bottom == 0);

    lock.write(ScreenRect{1, 2, 3, 4});
    ScreenRect r = lock.read();
    REQUIRE(r.left == 1);
    REQUIRE(r.top == 2);
    REQUIRE(r.right == 3);
    REQUIRE(r.bottom == 4);

    lock.write(ScreenRect{-100, -200, 300, 400});
    r = lock.read();
    REQUIRE(r.left == -100);
    REQUIRE(r.top == -200);
    REQUIRE(r.right == 300);
    REQUIRE(r.bottom == 400);
}

TEST_CASE("SeqLock never exposes a torn snapshot under concurrency", "[seqlock]")
{
    SeqLock<ScreenRect> lock;
    std::atomic<bool> done{false};
    std::atomic<long long> tornReads{0};
    std::atomic<long long> readCount{0};

    // Writer: publish {n,n,n,n} so every committed snapshot has four equal fields.
    std::thread writer([&] {
        for (int n = 1; n <= 2'000'000 && !done.load(std::memory_order_relaxed); ++n)
            lock.write(ScreenRect{n, n, n, n});
        done.store(true, std::memory_order_release);
    });

    // Reader: any mismatch between the four fields means a torn read slipped through.
    std::thread reader([&] {
        while (!done.load(std::memory_order_acquire))
        {
            ScreenRect r = lock.read();
            if (!(r.left == r.top && r.top == r.right && r.right == r.bottom))
                tornReads.fetch_add(1, std::memory_order_relaxed);
            readCount.fetch_add(1, std::memory_order_relaxed);
        }
    });

    writer.join();
    reader.join();

    // The reader must have actually run, and never observed a torn snapshot.
    REQUIRE(readCount.load() > 0);
    REQUIRE(tornReads.load() == 0);
}
