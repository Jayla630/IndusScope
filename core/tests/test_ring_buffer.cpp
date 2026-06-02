#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include "indusscope/core/RingBuffer.h"
#include "indusscope/core/SamplePoint.h"

using indusscope::core::RingBuffer;
using indusscope::core::SamplePoint;

// ---------------------------------------------------------------------------
// 1. Construction — capacity rounding to power of 2 / 构造——容量向上取整到 2 的幂
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer construction rounds capacity up to power of 2", "[core][ringbuffer]") {
    struct Case {
        std::size_t requested;
        std::size_t expected;
    };

    // Use a local array since Catch2 DATA_TABLE isn't available here / 用本地数组代替 Catch2 DATA_TABLE
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
// 2. Fill to capacity — full() then push rejects / 填满——full() 后再 push 被拒
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer fill and reject-on-full", "[core][ringbuffer]") {
    RingBuffer<int> rb(4);  // capacity → 4 (already pow2) / 容量 → 4 (已是 2 的幂)
    REQUIRE(rb.capacity() == 4);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(rb.push(i));
    }
    REQUIRE(rb.size() == rb.capacity());
    REQUIRE(rb.full());

    // Push on full buffer — should reject / 满缓冲上 push——应被拒绝
    REQUIRE(!rb.push(42));
    REQUIRE(rb.dropped() == 1);

    REQUIRE(!rb.push(99));
    REQUIRE(rb.dropped() == 2);

    // Size unchanged after rejected pushes / 被拒后 size 不变
    REQUIRE(rb.size() == rb.capacity());
}

// ---------------------------------------------------------------------------
// 3. FIFO ordering — push then pop preserves order / FIFO 顺序——push 后 pop 保持写入顺序
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
// 4. Wrap-around — cross the buffer end, verify order across boundary / 绕回——越过缓冲末尾,验证跨边界顺序
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer wrap-around", "[core][ringbuffer]") {
    RingBuffer<int> rb(4);  // capacity = 4 / 容量 = 4

    // Fill / 填满
    for (int i = 0; i < 4; ++i)
        rb.push(i);  // buf: [0, 1, 2, 3] / 缓冲: [0, 1, 2, 3]

    REQUIRE(rb.full());

    // Pop half / 弹出一半
    int val;
    rb.pop(val);  REQUIRE(val == 0);
    rb.pop(val);  REQUIRE(val == 1);

    REQUIRE(rb.size() == 2);

    // Push 2 more — wraps around to indices 0, 1 / 再推 2 个——绕回到索引 0, 1
    REQUIRE(rb.push(10));
    REQUIRE(rb.push(11));
    // buf: [10, 11, 2, 3], tail=2, head=6 / 缓冲: [10, 11, 2, 3], tail=2, head=6

    REQUIRE(rb.full());

    // Pop remaining — must be 2, 3, 10, 11 (FIFO across wrap) / 弹出剩余——必须是 2, 3, 10, 11 (跨绕回 FIFO)
    rb.pop(val);  REQUIRE(val == 2);
    rb.pop(val);  REQUIRE(val == 3);
    rb.pop(val);  REQUIRE(val == 10);
    rb.pop(val);  REQUIRE(val == 11);

    REQUIRE(rb.empty());
}

// ---------------------------------------------------------------------------
// 5. Pop from empty buffer — returns false, does not touch out / 空缓冲 pop——返回 false,不修改 out
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer pop from empty returns false", "[core][ringbuffer]") {
    RingBuffer<int> rb(4);

    int val = 42;
    REQUIRE(!rb.pop(val));
    // When pop fails, val must not be overwritten / pop 失败时 val 不能被覆盖
    REQUIRE(val == 42);

    // Push one, pop one, then try pop again / 推一个、弹一个,再尝试弹
    rb.push(7);
    REQUIRE(rb.pop(val));
    REQUIRE(val == 7);

    val = 99;
    REQUIRE(!rb.pop(val));
    REQUIRE(val == 99);
}

// ---------------------------------------------------------------------------
// 6. pop_batch — bulk extract / pop_batch——批量取出
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer pop_batch extracts correct count and order", "[core][ringbuffer]") {
    RingBuffer<int> rb(8);
    for (int i = 0; i < 5; ++i)
        rb.push(i + 1);  // 1,2,3,4,5 / 1,2,3,4,5

    int dst[10] = {};
    std::size_t n = rb.pop_batch(dst, 3);
    REQUIRE(n == 3);
    REQUIRE(dst[0] == 1);
    REQUIRE(dst[1] == 2);
    REQUIRE(dst[2] == 3);
    REQUIRE(rb.size() == 2);

    // Extract more than available / 请求数大于现存数
    n = rb.pop_batch(dst, 10);
    REQUIRE(n == 2);
    REQUIRE(dst[0] == 4);
    REQUIRE(dst[1] == 5);
    REQUIRE(rb.empty());
}

TEST_CASE("RingBuffer pop_batch across wrap boundary", "[core][ringbuffer]") {
    RingBuffer<int> rb(4);
    // Fill / 填满
    rb.push(1); rb.push(2); rb.push(3); rb.push(4);
    // Pop 2 / 弹出 2 个
    int dummy;
    rb.pop(dummy); rb.pop(dummy);
    // Push 2 more — wraps / 再推 2 个——绕回
    rb.push(5); rb.push(6);
    // Buffer: [5, 6, 3, 4], tail=2 → 3,4,5,6 / 缓冲: [5, 6, 3, 4], tail=2 → 3,4,5,6

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
    // dst untouched / dst 未被修改
    REQUIRE(dst[0] == 42);
}

// ---------------------------------------------------------------------------
// 7. Template instantiation with SamplePoint / 用 SamplePoint 模板实例化
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
// 8. Degenerate capacity 0 — rounds up to 1 / 退化容量 0——向上取整为 1
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
// 9. Degenerate capacity 1 — full lifecycle / 退化容量 1——完整生命周期
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer degenerate capacity 1 full lifecycle", "[core][ringbuffer]") {
    RingBuffer<int> rb(1);
    REQUIRE(rb.capacity() == 1);

    // Push the only slot — succeeds / 推入唯一的槽——成功
    REQUIRE(rb.push(42));
    REQUIRE(rb.size() == 1);
    REQUIRE(rb.full());

    // Push on full — rejected / 满时 push——被拒
    REQUIRE(!rb.push(99));
    REQUIRE(rb.dropped() == 1);

    // Pop — value correct, now empty / 弹出——值正确,缓冲变空
    int val = -1;
    REQUIRE(rb.pop(val));
    REQUIRE(val == 42);
    REQUIRE(rb.empty());
    REQUIRE(rb.size() == 0);

    // Pop from empty — rejected / 空时 pop——被拒
    REQUIRE(!rb.pop(val));
}

// ---------------------------------------------------------------------------
// 10. SPSC stress A — zero-drop with producer backoff, small capacity / SPSC 压测 A——生产者回退零丢,小容量强化绕回
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer SPSC stress: zero-drop with retry, FIFO integrity", "[core][ringbuffer][spsc][stress]") {
    constexpr std::size_t CAP = 64;
    constexpr std::size_t N   = 10'000'000;
    RingBuffer<std::uint64_t> rb(CAP);

    std::atomic<bool>     done{false};
    std::atomic<bool>     order_violated{false};
    std::atomic<std::uint64_t> received{0};

    // Producer: push 0..N-1, wait for space before each push (avoids push-rejection counting as "dropped")
    // 生产者:推入 0..N-1,每次 push 前等待空闲槽 (避免 push 被拒计入 dropped)
    // Using full() pre-check ensures push() always succeeds → dropped() stays 0.
    // 用 full() 预检保证 push() 总是成功 → dropped() 保持 0。
    std::thread producer([&]() {
        for (std::uint64_t i = 0; i < N; ++i) {
            while (rb.full()) {
                std::this_thread::yield();
            }
            rb.push(i); // guaranteed to succeed — we waited for space / 保证成功——已等待空闲槽
        }
        done.store(true, std::memory_order_release);
    });

    // Consumer: pop_batch for higher throughput (<1 atomic/item vs 2 in pop) / 消费者:用 pop_batch 提吞吐 (每次不足 1 次原子操作,pop 需 2 次)
    std::thread consumer([&]() {
        std::uint64_t last_value = 0;
        bool first = true;
        std::uint64_t batch_buf[256]; // local buffer / 本地缓冲
        while (!done.load(std::memory_order_acquire) || !rb.empty()) {
            std::size_t n = rb.pop_batch(batch_buf, 256);
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }
            for (std::size_t i = 0; i < n; ++i) {
                std::uint64_t val = batch_buf[i];
                // Check strict monotonicity — flag on first violation, REQUIRE once after join
                // 检查严格递增——首次违例记 flag,join 后只 REQUIRE 一次
                if (!first && !(val > last_value)) {
                    order_violated.store(true, std::memory_order_relaxed);
                }
                last_value = val;
                first = false;
            }
            received.fetch_add(n, std::memory_order_relaxed);
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(!order_violated.load());
    REQUIRE(received.load() == N);
    REQUIRE(rb.dropped() == 0);
}

// ---------------------------------------------------------------------------
// 11. SPSC stress B — forced drops, small capacity, throttled consumer / SPSC 压测 B——强制丢点,小容量,消费者节流
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer SPSC stress: forced-drop with throttled consumer", "[core][ringbuffer][spsc][stress]") {
    constexpr std::size_t CAP = 256;
    constexpr std::size_t N   = 10'000'000;
    RingBuffer<std::uint64_t> rb(CAP);

    std::atomic<bool>     done{false};
    std::atomic<bool>     order_violated{false};
    std::atomic<std::uint64_t> received{0};

    // Producer: blind push, ignore return / 生产者:盲推,忽略返回值
    std::thread producer([&]() {
        for (std::uint64_t i = 0; i < N; ++i) {
            rb.push(i); // ignore return — forced drops / 忽略返回值——强制丢点
        }
        done.store(true, std::memory_order_release);
    });

    // Consumer: throttled, check monotonicity / 消费者:节流,检查递增
    std::thread consumer([&]() {
        std::uint64_t last_value = 0;
        bool first = true;
        std::uint64_t local_pops = 0;
        while (!done.load(std::memory_order_acquire) || !rb.empty()) {
            std::uint64_t val;
            if (!rb.pop(val)) {
                std::this_thread::yield();
                continue;
            }
            // Check strict monotonicity — flag on first violation / 检查严格递增——首次违例记 flag
            if (!first && !(val > last_value)) {
                order_violated.store(true, std::memory_order_relaxed);
            }
            last_value = val;
            first = false;
            ++local_pops;
            // Throttle every 4096 pops — µs-level sleep guarantees producer overflow / 每 4096 次 pop 节流(微秒 sleep)——强制溢出
            if (local_pops % 4096 == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds{100});
            }
        }
        received.store(local_pops, std::memory_order_relaxed);
    });

    producer.join();
    consumer.join();

    std::uint64_t recv = received.load();
    std::uint64_t drop = rb.dropped();

    REQUIRE(!order_violated.load());
    REQUIRE(recv == N - drop);
    REQUIRE(drop > 0);
}

// ---------------------------------------------------------------------------
// 12. Single-threaded throughput benchmark — push only / 单线程吞吐基准——仅推入
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer single-threaded push throughput benchmark", "[core][ringbuffer][bench]") {
    constexpr std::size_t N = 5'000'000;
    RingBuffer<std::uint64_t> rb(65536); // large capacity, never full / 大容量,永不填满

    auto t0 = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < N; ++i) {
        rb.push(i);
    }
    auto t1 = std::chrono::steady_clock::now();

    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    double ops_per_sec = (double)N / (double)elapsed_ns * 1e9;

    // Minimal sanity: push must be fast enough (> 1M ops/s on any machine) / 最低健康检查:任何机器上都应 > 1M ops/s
    REQUIRE(ops_per_sec > 1'000'000.0);

    // Print for manual recording into BENCHMARK.md / 打印供手动录入 BENCHMARK.md
    std::cout << "[bench] Single-thread push: " << N << " items in " << elapsed_ns
              << " ns → " << (ops_per_sec / 1e6) << " M ops/s" << std::endl;
}
