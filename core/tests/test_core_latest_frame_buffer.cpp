#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <optional>
#include <thread>

#include "indusscope/core/ImageFrame.h"
#include "indusscope/core/LatestFrameBuffer.h"

using namespace indusscope::core;

// --- Global allocation counter 全局分配计数 ---
// Tracks operator new only while g_alloc_track is on, so the measured hot loop can
// prove zero allocation without Catch2's own allocations polluting the count.
// 仅在 g_alloc_track 打开时计数 operator new,使被测热循环能在不被 Catch2 自身分配
// 干扰的情况下证明零分配。
namespace {
std::atomic<bool>        g_alloc_track{false};
std::atomic<std::size_t> g_alloc_count{0};
} // namespace

void* operator new(std::size_t n) {
    if (g_alloc_track.load(std::memory_order_relaxed))
        g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n != 0 ? n : 1);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

// Write a whole-frame sentinel keyed on @p seq: a 64-bit seq in the first 8 bytes,
// then every body byte = (seq & 0xFF). A torn read (bytes from different seqs)
// fails the consumer's cross-frame consistency check.
// 写整帧哨兵(以 @p seq 为钥):前 8 字节存 64 位 seq,其余 body 每字节 = (seq & 0xFF)。
// 撕裂读(字节来自不同 seq)会被消费者的整帧一致性检查抓出。
void fillSentinel(const ImageFrame& f, std::uint64_t seq) {
    std::byte* base = f.data;
    const std::size_t total = static_cast<std::size_t>(f.stride) * static_cast<std::size_t>(f.height);
    const std::byte v = static_cast<std::byte>(seq & 0xFF);
    for (std::size_t i = 8; i < total; ++i)
        base[i] = v;
    std::uint64_t s = seq;
    for (int b = 0; b < 8; ++b) { // little-endian seq header / 小端 seq 帧头
        base[b] = static_cast<std::byte>(s & 0xFF);
        s >>= 8;
    }
}

// Read the sentinel seq and verify whole-frame consistency. Returns the seq if the
// frame is internally consistent (no tearing), or 0 if torn. Producer seq starts at
// 1, so 0 unambiguously means "torn".
// 读哨兵 seq 并校验整帧一致性。整帧一致(无撕裂)返回 seq,撕裂返回 0。
// 生产者 seq 从 1 起,故 0 明确表示"撕裂"。
std::uint64_t checkSentinel(const ImageFrame& f) {
    const std::byte* base = f.data;
    std::uint64_t seq = 0;
    for (int b = 7; b >= 0; --b)
        seq = (seq << 8) | static_cast<std::uint8_t>(base[b]);

    const std::byte v = static_cast<std::byte>(seq & 0xFF);
    const std::size_t total = static_cast<std::size_t>(f.stride) * static_cast<std::size_t>(f.height);
    // Sample body bytes spread across the frame: first, quarters, middle, last.
    // 抽样散布全帧的 body 字节:首、四分位、中、尾。
    const std::size_t pts[] = {8, total / 4, total / 2, (total * 3) / 4, total - 1};
    for (std::size_t p : pts) {
        if (p >= 8 && base[p] != v)
            return 0; // torn / 撕裂
    }
    return seq;
}

} // namespace

// (a)+(b) No tearing + monotonic, never-regressing frame index under 2 real threads.
// (a)+(b) 双真线程下无撕裂 + 帧号单调不退。
TEST_CASE("LatestFrameBuffer: no tearing & monotonic under two threads", "[latest_frame_buffer][concurrency]") {
    constexpr std::int32_t W = 64, H = 48;
    constexpr std::uint64_t N = 300000;
    LatestFrameBuffer buf(W, H);

    std::atomic<bool>          producer_done{false};
    std::atomic<std::uint64_t> torn{0};
    std::atomic<std::uint64_t> regressed{0};
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<std::uint64_t> final_seq{0};

    std::thread producer([&] {
        for (std::uint64_t seq = 1; seq <= N; ++seq) {
            fillSentinel(buf.writeSlot(), seq);
            buf.commit(static_cast<std::int64_t>(seq));
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::uint64_t last = 0;
        for (;;) {
            std::optional<ImageFrame> f = buf.takeLatest();
            if (!f) {
                if (producer_done.load(std::memory_order_acquire)) {
                    f = buf.takeLatest(); // final drain after producer finished / 生产者收工后最后清场
                    if (!f)
                        break;
                } else {
                    continue; // spin until a frame is ready / 自旋等下一帧
                }
            }
            const std::uint64_t seq = checkSentinel(*f);
            if (seq == 0) {
                torn.fetch_add(1, std::memory_order_relaxed);
            } else {
                if (seq < last)
                    regressed.fetch_add(1, std::memory_order_relaxed);
                last = seq;
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }
        final_seq.store(last, std::memory_order_relaxed);
    });

    producer.join();
    consumer.join();

    REQUIRE(torn.load() == 0);              // (a) every taken frame internally consistent / 取到的每帧整帧一致
    REQUIRE(regressed.load() == 0);         // (b) frame index never regressed / 帧号从不回退
    REQUIRE(consumed.load() > 0);           // consumer actually saw frames / 消费者确实看到帧
    REQUIRE(final_seq.load() == N);         // last committed frame (N) is always delivered / 最后提交帧(N)必送达
    REQUIRE(consumed.load() <= N);          // latest-wins may skip, never duplicate-inflate / latest-wins 可跳号,绝不虚增
}

// (c) Producer never blocks: a deliberately slow consumer must cause drops, proving
// the producer ran full-speed instead of waiting for the renderer.
// (c) 生产者永不阻塞:故意拖慢消费者必产生丢帧,证明生产者全速跑而非等渲染。
TEST_CASE("LatestFrameBuffer: producer never blocks; slow consumer drops frames", "[latest_frame_buffer][dropframes]") {
    LatestFrameBuffer buf(32, 24);
    constexpr std::uint64_t N = 200000;

    std::atomic<bool> producer_done{false};

    std::thread producer([&] {
        for (std::uint64_t seq = 1; seq <= N; ++seq) {
            buf.writeSlot().data[0] = static_cast<std::byte>(seq & 0xFF);
            buf.commit(static_cast<std::int64_t>(seq));
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        while (!producer_done.load(std::memory_order_acquire)) {
            (void)buf.takeLatest();
            std::this_thread::sleep_for(std::chrono::microseconds(50)); // deliberately slow / 故意慢
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(buf.dropped() > 0); // latest-wins really dropped; producer wasn't throttled / 真丢帧,生产者没被拖
}

// (d) Zero allocation on the commit/takeLatest hot path. The measured loop body has
// NO REQUIRE/CHECK and no new; assertions run outside the tracked section.
// (d) commit/takeLatest 热路径零分配。被测循环体内无 REQUIRE/CHECK、无 new;断言在
//     计数区间之外执行。
TEST_CASE("LatestFrameBuffer: commit/takeLatest allocate nothing", "[latest_frame_buffer][noalloc]") {
    LatestFrameBuffer buf(64, 48);

    // Warmup outside the tracked section. / 预热,在计数区间外。
    buf.writeSlot().data[0] = std::byte{0};
    buf.commit(1);
    (void)buf.takeLatest();

    std::size_t taken = 0;
    g_alloc_count.store(0, std::memory_order_relaxed);
    g_alloc_track.store(true, std::memory_order_relaxed);
    for (int i = 0; i < 100000; ++i) {
        buf.writeSlot().data[0] = static_cast<std::byte>(i & 0xFF);
        buf.commit(i);
        std::optional<ImageFrame> f = buf.takeLatest();
        if (f)
            ++taken;
    }
    g_alloc_track.store(false, std::memory_order_relaxed);

    REQUIRE(g_alloc_count.load() == 0); // no heap allocation in the hot path / 热路径零堆分配
    REQUIRE(taken > 0);                 // loop actually exercised takeLatest / 循环确实跑了 takeLatest
}

// (e) Geometry/observer sanity + single-thread roundtrip + latest-wins semantics.
// (e) 几何/观察者 + 单线程往返 + latest-wins 语义。
TEST_CASE("LatestFrameBuffer: single-thread roundtrip and latest-wins", "[latest_frame_buffer][basic]") {
    LatestFrameBuffer buf(8, 6);
    REQUIRE(buf.width() == 8);
    REQUIRE(buf.height() == 6);
    REQUIRE(buf.format() == PixelFormat::RGBA8888);
    REQUIRE(buf.stride() == aligned_stride(8, PixelFormat::RGBA8888));

    // Nothing committed yet → nullopt. / 还没提交 → nullopt。
    REQUIRE_FALSE(buf.takeLatest());

    // Commit one, take it back with its timestamp. / 提交一帧,连时间戳取回。
    buf.writeSlot().data[0] = std::byte{0x11};
    buf.commit(12345);
    std::optional<ImageFrame> f = buf.takeLatest();
    REQUIRE(f);
    REQUIRE(f->timestamp_ns == 12345);
    REQUIRE(f->data[0] == std::byte{0x11});

    // No new commit → nullopt again (keep current). / 无新提交 → 再次 nullopt(保留当前)。
    REQUIRE_FALSE(buf.takeLatest());

    // Two commits, one take → the second wins, the first is dropped.
    // 连提交两帧,只取一次 → 取到第二帧,第一帧被丢。
    buf.writeSlot().data[0] = std::byte{0x22};
    buf.commit(1);
    buf.writeSlot().data[0] = std::byte{0x33};
    buf.commit(2);
    std::optional<ImageFrame> g = buf.takeLatest();
    REQUIRE(g);
    REQUIRE(g->data[0] == std::byte{0x33}); // latest wins / 取最新
    REQUIRE(buf.dropped() == 1);            // the 0x22 frame was dropped / 0x22 帧被丢
}
