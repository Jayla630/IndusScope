#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "indusscope/core/MockSource.h"
#include "indusscope/core/SignalGenerator.h"

#include <cmath>

using indusscope::core::MockSource;
using indusscope::core::MockSourceConfig;
using indusscope::core::RingBuffer;
using indusscope::core::RunStats;
using indusscope::core::SamplePoint;
using indusscope::core::SignalConfig;
using indusscope::core::SignalGenerator;

// Tolerance for value comparisons / 值比对容差
constexpr double kEps = 1e-9;

// --- Helper: drain RingBuffer into an array, return count ---
// --- 辅助:将 RingBuffer 排到数组,返回个数 ---
template <std::size_t MaxN>
static std::size_t drain(RingBuffer<SamplePoint>& rb, SamplePoint (&dst)[MaxN]) {
    return rb.pop_batch(dst, MaxN);
}

// ---------------------------------------------------------------------------
// 1. Timestamp grid correctness / 时间戳网格正确性
//    produce(n), drain, verify ts == start + i*period (exact integer).
//    produce(n), drain, 验证 ts == start + i*period (精确整数比对)。
// ---------------------------------------------------------------------------

TEST_CASE("MockSource timestamp grid correctness", "[core][mock_source]") {
    MockSourceConfig cfg;
    cfg.sample_rate_hz = 1000.0;       // period = 1'000'000 ns / 周期 = 1'000'000 ns
    cfg.start_timestamp_ns = 1'000'000'000;

    constexpr std::size_t N = 100;
    RingBuffer<SamplePoint> sink(N);    // capacity >= N, no drops / 容量 >= N,不丢点
    MockSource src(cfg, sink);

    src.produce(N);
    REQUIRE(src.produced() == N);
    REQUIRE(src.dropped() == 0);
    REQUIRE(src.index() == N);

    SamplePoint buf[N];
    std::size_t got = drain(sink, buf);
    REQUIRE(got == N);

    constexpr std::int64_t period = 1'000'000; // 1e9 / 1000 / 1e9 / 1000
    for (std::size_t i = 0; i < N; ++i) {
        std::int64_t expected_ts = cfg.start_timestamp_ns +
                                   static_cast<std::int64_t>(i) * period;
        REQUIRE(buf[i].timestamp_ns == expected_ts);
    }
}

// ---------------------------------------------------------------------------
// 2. Value equivalence vs SignalGenerator / 值等价——对比 SignalGenerator
//    σ=0 config; produce(n); each value == SignalGenerator.value(same ts).
//    σ=0 配置;produce(n);每个 value == SignalGenerator.value(同 ts)。
// ---------------------------------------------------------------------------

TEST_CASE("MockSource value equivalence vs SignalGenerator", "[core][mock_source]") {
    MockSourceConfig cfg;
    cfg.sample_rate_hz = 500.0;        // period = 2'000'000 ns / 周期 = 2'000'000 ns
    cfg.start_timestamp_ns = 500'000'000;
    cfg.signal_config.amplitude = 2.5;
    cfg.signal_config.frequency_hz = 10.0;
    cfg.signal_config.phase_rad = 0.3;
    cfg.signal_config.offset = 0.5;
    cfg.signal_config.noise_stddev = 0.0; // deterministic / 确定性

    constexpr std::size_t N = 50;
    RingBuffer<SamplePoint> sink(N);
    MockSource src(cfg, sink);

    // Reference generator with same config / 同配置的参考发生器
    SignalGenerator ref_gen(cfg.signal_config);

    src.produce(N);

    SamplePoint buf[N];
    std::size_t got = drain(sink, buf);
    REQUIRE(got == N);

    for (std::size_t i = 0; i < N; ++i) {
        double expected_v = ref_gen.value(buf[i].timestamp_ns);
        REQUIRE_THAT(buf[i].value, Catch::Matchers::WithinAbs(expected_v, kEps));
    }
}

// ---------------------------------------------------------------------------
// 3. Buffer full — rejection + counts / 满则丢——拒绝 + 计数
//    sink capacity < n; produced + dropped == n; dropped consistent.
//    sink 容量 < n;produced + dropped == n;dropped 语义一致。
// ---------------------------------------------------------------------------

TEST_CASE("MockSource buffer full rejection and counts", "[core][mock_source]") {
    MockSourceConfig cfg;
    cfg.sample_rate_hz = 1000.0;

    constexpr std::size_t Cap = 10;
    constexpr std::size_t N = 25;         // N > Cap, expect drops / N > Cap,期望丢点
    RingBuffer<SamplePoint> sink(Cap);
    MockSource src(cfg, sink);

    // RingBuffer rounds capacity up to power of 2 / RingBuffer 向上取整到 2 的幂
    std::size_t actual_cap = sink.capacity(); // 10 → 16
    REQUIRE(actual_cap > Cap);

    src.produce(N);

    REQUIRE(src.produced() + src.dropped() == N);
    REQUIRE(src.index() == N);
    REQUIRE(src.produced() == actual_cap); // first actual_cap samples fit / 前 actual_cap 个塞进去
    REQUIRE(src.dropped() == N - actual_cap); // rest rejected / 剩余被拒
    REQUIRE(sink.full());
}

// ---------------------------------------------------------------------------
// 4. produce/run_for sequence equivalence / produce/run_for 序列等价
//    σ=0, same config; produce(k) sequence == first k from run_for drain.
//    σ=0,同配置;produce(k) 序列 == run_for 后 drain 的前 k 个。
// ---------------------------------------------------------------------------

TEST_CASE("MockSource produce and run_for produce identical sequences", "[core][mock_source]") {
    MockSourceConfig cfg;
    cfg.sample_rate_hz = 1000.0;
    cfg.start_timestamp_ns = 0;
    cfg.signal_config.amplitude = 3.0;
    cfg.signal_config.frequency_hz = 5.0;
    cfg.signal_config.phase_rad = 1.0;
    cfg.signal_config.offset = 0.1;
    cfg.signal_config.noise_stddev = 0.0;

    constexpr std::size_t K = 100;
    constexpr std::size_t Cap = 500;

    // --- produce(k) ---
    RingBuffer<SamplePoint> sink_a(Cap);
    MockSource src_a(cfg, sink_a);
    src_a.produce(K);
    SamplePoint buf_a[Cap];
    std::size_t got_a = drain(sink_a, buf_a);
    REQUIRE(got_a == K);

    // --- run_for (~500ms to guarantee ≥ K samples) ---
    // --- run_for (~500ms,保证产出 ≥ K 个样本) ---
    RingBuffer<SamplePoint> sink_b(Cap);
    MockSource src_b(cfg, sink_b);
    RunStats stats = src_b.run_for(std::chrono::milliseconds(500));
    REQUIRE(stats.produced >= K);          // ample time for K @ 1kHz / 1kHz 下时间足够

    // Drain and compare first K / drain 后比较前 K 个
    SamplePoint buf_b[Cap];
    std::size_t got_b = drain(sink_b, buf_b);
    REQUIRE(got_b >= K);

    for (std::size_t i = 0; i < K; ++i) {
        REQUIRE(buf_a[i].timestamp_ns == buf_b[i].timestamp_ns);
        REQUIRE_THAT(buf_a[i].value, Catch::Matchers::WithinAbs(buf_b[i].value, kEps));
    }
}

// ---------------------------------------------------------------------------
// 5. Timed smoke test (loose) / 定时烟雾测 (宽松)
//    run_for(200ms) @ 1000Hz; produced >= 1, timestamps increasing,
//    achieved_rate > 0 and finite, produced in [60, 400].
//    run_for(200ms) @ 1000Hz;produced >= 1,时间戳严格递增,
//    achieved_rate > 0 且有限,produced ∈ [60, 400]。
// ---------------------------------------------------------------------------

TEST_CASE("MockSource timed smoke test @ 1000Hz for 200ms", "[core][mock_source][smoke]") {
    MockSourceConfig cfg;
    cfg.sample_rate_hz = 1000.0;
    cfg.signal_config.noise_stddev = 0.0;

    constexpr std::size_t Cap = 1024;
    RingBuffer<SamplePoint> sink(Cap);
    MockSource src(cfg, sink);

    RunStats stats = src.run_for(std::chrono::milliseconds(200));

    // Loose assertions — this is NOT a hard-real-time test / 宽松断言——这不是硬实时测试
    REQUIRE(stats.produced >= 1);
    REQUIRE(stats.dropped == 0);     // capacity >> expected / 容量远大于预期产出
    REQUIRE(stats.elapsed_ns > 0);
    REQUIRE(stats.achieved_rate_hz > 0.0);
    REQUIRE(std::isfinite(stats.achieved_rate_hz));

    // Wide window: nominal 200 samples, allow [60, 400] / 宽窗口:名义 200,允许 [60, 400]
    REQUIRE(stats.produced >= 60);
    REQUIRE(stats.produced <= 400);

    // Drain and verify timestamps are strictly increasing / drain 并验证时间戳严格递增
    SamplePoint buf[Cap];
    std::size_t got = drain(sink, buf);
    REQUIRE(got == stats.produced);

    for (std::size_t i = 1; i < got; ++i) {
        REQUIRE(buf[i].timestamp_ns > buf[i - 1].timestamp_ns);
    }
}

// ---------------------------------------------------------------------------
// 6. Counter reset / 计数器重置
//    produce then reset; produced/dropped/index all zero.
//    produce 后 reset;produced/dropped/index 全部归零。
// ---------------------------------------------------------------------------

TEST_CASE("MockSource reset zeros counters", "[core][mock_source]") {
    MockSourceConfig cfg;
    cfg.sample_rate_hz = 1000.0;

    RingBuffer<SamplePoint> sink(64);
    MockSource src(cfg, sink);

    src.produce(30);
    REQUIRE(src.produced() == 30);
    REQUIRE(src.index() == 30);

    src.reset();

    REQUIRE(src.produced() == 0);
    REQUIRE(src.dropped() == 0);
    REQUIRE(src.index() == 0);
}

// ---------------------------------------------------------------------------
// 7. Config hot-swap — full reset / 配置热切换——全量重置
//    set_config resets counters + generator; index()==0 after.
//    set_config 重置计数器 + 发生器;之后 index()==0。
// ---------------------------------------------------------------------------

TEST_CASE("MockSource set_config does full reset, index zero after", "[core][mock_source]") {
    MockSourceConfig cfg;
    cfg.sample_rate_hz = 1000.0;
    cfg.signal_config.amplitude = 1.0;

    RingBuffer<SamplePoint> sink(64);
    MockSource src(cfg, sink);

    src.produce(20);
    REQUIRE(src.index() == 20);

    // Swap to a different config / 切换到不同配置
    MockSourceConfig cfg2;
    cfg2.sample_rate_hz = 2000.0;
    cfg2.signal_config.amplitude = 5.0;
    src.set_config(cfg2);

    // Full reset verified / 验证全量重置
    REQUIRE(src.index() == 0);
    REQUIRE(src.produced() == 0);
    REQUIRE(src.dropped() == 0);

    // Fresh config takes effect / 新配置生效
    REQUIRE(src.config().sample_rate_hz == 2000.0);
    REQUIRE(src.config().signal_config.amplitude == 5.0);

    // Produce with fresh config works / 用新配置产出正常
    src.produce(5);
    REQUIRE(src.produced() == 5);
    REQUIRE(src.index() == 5);

    // Verify period changed: old 1ms -> fresh 0.5ms / 验证周期已变:旧 1ms → 新 0.5ms
    SamplePoint buf[64];
    std::size_t got = drain(sink, buf);
    REQUIRE(got >= 5);
    // First sample after set_config: index was 0 → ts = start_ns + 0*500000 = 0
    // set_config 后第一个样本:index 已归零 → ts = start_ns + 0*500000 = 0
    REQUIRE(buf[0].timestamp_ns == cfg2.start_timestamp_ns);
}
