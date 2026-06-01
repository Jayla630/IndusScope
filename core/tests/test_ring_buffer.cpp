#include <catch2/catch_test_macros.hpp>
#include "indusscope/core/RingBuffer.h"
#include "indusscope/core/SamplePoint.h"

using indusscope::core::RingBuffer;
using indusscope::core::SamplePoint;

// ---------------------------------------------------------------------------
// 1. Construction — capacity rounding to power of 2
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer construction rounds capacity up to power of 2", "[core][ringbuffer]") {
    struct Case {
        std::size_t requested;
        std::size_t expected;
    };

    // Use a local array since Catch2 DATA_TABLE isn't available here
    Case cases[] = {
        {1,    1},
        {2,    2},
        {3,    4},
        {100, 128},
        {255, 256},
        {256, 256},
        {500, 512},
        {1023, 1024},
        {1024, 1024},
        {4095, 4096},
    };

    for (auto [req, exp] : cases) {
        DYNAMIC_SECTION("capacity " << req << " → " << exp) {
            RingBuffer<int> rb(req);
            REQUIRE(rb.capacity() == exp);
            REQUIRE(rb.size() == 0);
            REQUIRE(rb.empty());
            REQUIRE(!rb.full());
            REQUIRE(rb.dropped() == 0);
        }
    }
}

// ---------------------------------------------------------------------------
// 2. Fill to capacity — full() then push rejects
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer fill and reject-on-full", "[core][ringbuffer]") {
    RingBuffer<int> rb(4);  // capacity → 4 (already pow2)
    REQUIRE(rb.capacity() == 4);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(rb.push(i));
    }
    REQUIRE(rb.size() == rb.capacity());
    REQUIRE(rb.full());

    // Push on full buffer — should reject
    REQUIRE(!rb.push(42));
    REQUIRE(rb.dropped() == 1);

    REQUIRE(!rb.push(99));
    REQUIRE(rb.dropped() == 2);

    // Size unchanged after rejected pushes
    REQUIRE(rb.size() == rb.capacity());
}

// ---------------------------------------------------------------------------
// 3. FIFO ordering — push then pop preserves order
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer FIFO ordering", "[core][ringbuffer]") {
    RingBuffer<int> rb(8);
    constexpr int N = 6;

    for (int i = 0; i < N; ++i)
        rb.push(i * 10);

    REQUIRE(rb.size() == N);

    for (int i = 0; i < N; ++i) {
        int val = -1;
        REQUIRE(rb.pop(val));
        REQUIRE(val == i * 10);
    }

    REQUIRE(rb.empty());
}

// ---------------------------------------------------------------------------
// 4. Wrap-around — cross the buffer end, verify order across boundary
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer wrap-around", "[core][ringbuffer]") {
    RingBuffer<int> rb(4);  // capacity = 4

    // Fill
    for (int i = 0; i < 4; ++i)
        rb.push(i);  // buf: [0, 1, 2, 3]

    REQUIRE(rb.full());

    // Pop half
    int val;
    rb.pop(val);  REQUIRE(val == 0);
    rb.pop(val);  REQUIRE(val == 1);

    REQUIRE(rb.size() == 2);

    // Push 2 more — wraps around to indices 0, 1
    REQUIRE(rb.push(10));
    REQUIRE(rb.push(11));
    // buf: [10, 11, 2, 3], tail=2, head=6

    REQUIRE(rb.full());

    // Pop remaining — must be 2, 3, 10, 11 (FIFO across wrap)
    rb.pop(val);  REQUIRE(val == 2);
    rb.pop(val);  REQUIRE(val == 3);
    rb.pop(val);  REQUIRE(val == 10);
    rb.pop(val);  REQUIRE(val == 11);

    REQUIRE(rb.empty());
}

// ---------------------------------------------------------------------------
// 5. Pop from empty buffer — returns false, does not touch out
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer pop from empty returns false", "[core][ringbuffer]") {
    RingBuffer<int> rb(4);

    int val = 42;
    REQUIRE(!rb.pop(val));
    // When pop fails, val must not be overwritten
    REQUIRE(val == 42);

    // Push one, pop one, then try pop again
    rb.push(7);
    REQUIRE(rb.pop(val));
    REQUIRE(val == 7);

    val = 99;
    REQUIRE(!rb.pop(val));
    REQUIRE(val == 99);
}

// ---------------------------------------------------------------------------
// 6. pop_batch — bulk extract
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer pop_batch extracts correct count and order", "[core][ringbuffer]") {
    RingBuffer<int> rb(8);
    for (int i = 0; i < 5; ++i)
        rb.push(i + 1);  // 1,2,3,4,5

    int dst[10] = {};
    std::size_t n = rb.pop_batch(dst, 3);
    REQUIRE(n == 3);
    REQUIRE(dst[0] == 1);
    REQUIRE(dst[1] == 2);
    REQUIRE(dst[2] == 3);
    REQUIRE(rb.size() == 2);

    // Extract more than available
    n = rb.pop_batch(dst, 10);
    REQUIRE(n == 2);
    REQUIRE(dst[0] == 4);
    REQUIRE(dst[1] == 5);
    REQUIRE(rb.empty());
}

TEST_CASE("RingBuffer pop_batch across wrap boundary", "[core][ringbuffer]") {
    RingBuffer<int> rb(4);
    // Fill
    rb.push(1); rb.push(2); rb.push(3); rb.push(4);
    // Pop 2
    int dummy;
    rb.pop(dummy); rb.pop(dummy);
    // Push 2 more — wraps
    rb.push(5); rb.push(6);
    // Buffer: [5, 6, 3, 4], tail=2 → 3,4,5,6

    int dst[10] = {};
    std::size_t n = rb.pop_batch(dst, 10);
    REQUIRE(n == 4);
    REQUIRE(dst[0] == 3);
    REQUIRE(dst[1] == 4);
    REQUIRE(dst[2] == 5);
    REQUIRE(dst[3] == 6);
    REQUIRE(rb.empty());
}

TEST_CASE("RingBuffer pop_batch from empty returns zero", "[core][ringbuffer]") {
    RingBuffer<int> rb(4);
    int dst[4] = {42, 42, 42, 42};
    std::size_t n = rb.pop_batch(dst, 4);
    REQUIRE(n == 0);
    // dst untouched
    REQUIRE(dst[0] == 42);
}

// ---------------------------------------------------------------------------
// 7. Template instantiation with SamplePoint
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer<SamplePoint> compiles and works", "[core][ringbuffer]") {
    RingBuffer<SamplePoint> rb(4);

    SamplePoint sp{1000, 3.14};
    REQUIRE(rb.push(sp));

    sp = {2000, 2.718};
    REQUIRE(rb.push(sp));

    REQUIRE(rb.size() == 2);

    SamplePoint out{};
    REQUIRE(rb.pop(out));
    REQUIRE(out.timestamp_ns == 1000);
    REQUIRE(out.value == 3.14);

    REQUIRE(rb.pop(out));
    REQUIRE(out.timestamp_ns == 2000);
    REQUIRE(out.value == 2.718);

    REQUIRE(rb.empty());
}

// ---------------------------------------------------------------------------
// 8. Degenerate capacity 0 — rounds up to 1
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer degenerate capacity 0 rounds up to 1", "[core][ringbuffer]") {
    RingBuffer<int> rb(0);
    REQUIRE(rb.capacity() == 1);
    REQUIRE(rb.size() == 0);
    REQUIRE(rb.empty());
    REQUIRE(!rb.full());
    REQUIRE(rb.dropped() == 0);
}

// ---------------------------------------------------------------------------
// 9. Degenerate capacity 1 — full lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer degenerate capacity 1 full lifecycle", "[core][ringbuffer]") {
    RingBuffer<int> rb(1);
    REQUIRE(rb.capacity() == 1);

    // Push the only slot — succeeds
    REQUIRE(rb.push(42));
    REQUIRE(rb.size() == 1);
    REQUIRE(rb.full());

    // Push on full — rejected
    REQUIRE(!rb.push(99));
    REQUIRE(rb.dropped() == 1);

    // Pop — value correct, now empty
    int val = -1;
    REQUIRE(rb.pop(val));
    REQUIRE(val == 42);
    REQUIRE(rb.empty());
    REQUIRE(rb.size() == 0);

    // Pop from empty — rejected
    REQUIRE(!rb.pop(val));
}
