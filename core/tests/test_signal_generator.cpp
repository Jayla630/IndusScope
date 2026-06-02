#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "indusscope/core/SignalGenerator.h"

#include <cmath>

using indusscope::core::SignalConfig;
using indusscope::core::SignalGenerator;

// Test tolerance for pure-sine comparisons / 纯正弦比对的测试容差
constexpr double kEps = 1e-9;

// Number of samples for statistical tests / 统计测试的采样数
constexpr int kStatsN = 100'000;

// ---------------------------------------------------------------------------
// 1. Pure sine — amplitude at quarter cycle / 纯正弦——1/4 周期处的幅值
//    σ=0, A=1, f=1Hz, φ=0: t=250ms → sin(π/2)=1 → value ≈ 1.0
// ---------------------------------------------------------------------------

TEST_CASE("SignalGenerator pure sine -- quarter cycle (A=1, f=1Hz)", "[core][signal_generator]") {
    SignalConfig cfg;
    cfg.amplitude = 1.0;
    cfg.frequency_hz = 1.0;
    cfg.phase_rad = 0.0;
    cfg.offset = 0.0;
    cfg.noise_stddev = 0.0;

    SignalGenerator gen(cfg);

    // t = 0.25s → sin(2π·1·0.25) = sin(π/2) = 1.0 / t = 0.25s → sin(2π·1·0.25) = sin(π/2) = 1.0
    double v = gen.value(250'000'000);
    REQUIRE_THAT(v, Catch::Matchers::WithinAbs(1.0, kEps));

    // t = 0.5s → sin(π) = 0.0 / t = 0.5s → sin(π) = 0.0
    v = gen.value(500'000'000);
    REQUIRE_THAT(v, Catch::Matchers::WithinAbs(0.0, kEps));

    // t = 0.75s → sin(3π/2) = -1.0 / t = 0.75s → sin(3π/2) = -1.0
    v = gen.value(750'000'000);
    REQUIRE_THAT(v, Catch::Matchers::WithinAbs(-1.0, kEps));

    // t = 1.0s → sin(2π) = 0.0 (full period) / t = 1.0s → sin(2π) = 0.0 (完整周期)
    v = gen.value(1'000'000'000);
    REQUIRE_THAT(v, Catch::Matchers::WithinAbs(0.0, kEps));
}

// ---------------------------------------------------------------------------
// 2. Pure sine — amplitude & frequency scaling / 纯正弦——幅值与频率缩放
//    A=2, f=2Hz, φ=0: t=125ms → sin(2π·2·0.125) = sin(π/2) = 1 → 2·1 = 2.0
// ---------------------------------------------------------------------------

TEST_CASE("SignalGenerator pure sine -- amplitude and frequency scaling", "[core][signal_generator]") {
    SignalConfig cfg;
    cfg.amplitude = 2.0;
    cfg.frequency_hz = 2.0;
    cfg.phase_rad = 0.0;
    cfg.offset = 0.0;
    cfg.noise_stddev = 0.0;

    SignalGenerator gen(cfg);

    // t = 0.125s → sin(2π·2·0.125) = sin(π/2) = 1 → A·1 = 2.0
    double v = gen.value(125'000'000);
    REQUIRE_THAT(v, Catch::Matchers::WithinAbs(2.0, kEps));

    // t = 0.25s → sin(π) = 0 → 0.0
    v = gen.value(250'000'000);
    REQUIRE_THAT(v, Catch::Matchers::WithinAbs(0.0, kEps));
}

// ---------------------------------------------------------------------------
// 3. Phase offset / 相位偏移
//    A=1, f=1Hz, φ=π/2: t=0 → sin(π/2) = 1.0
// ---------------------------------------------------------------------------

TEST_CASE("SignalGenerator phase offset", "[core][signal_generator]") {
    SignalConfig cfg;
    cfg.amplitude = 1.0;
    cfg.frequency_hz = 1.0;
    cfg.phase_rad = 1.5707963267948966; // π/2
    cfg.offset = 0.0;
    cfg.noise_stddev = 0.0;

    SignalGenerator gen(cfg);

    // t=0: sin(0 + π/2) = 1.0
    double v = gen.value(0);
    REQUIRE_THAT(v, Catch::Matchers::WithinAbs(1.0, kEps));

    // t=0.25s: sin(π/2 + π/2) = sin(π) = 0.0
    v = gen.value(250'000'000);
    REQUIRE_THAT(v, Catch::Matchers::WithinAbs(0.0, kEps));
}

// ---------------------------------------------------------------------------
// 4. DC offset / 直流偏置
//    A=0 (no sine), σ=0 (no noise), offset=5.0 → value() ≡ 5.0
// ---------------------------------------------------------------------------

TEST_CASE("SignalGenerator DC offset (A=0, σ=0)", "[core][signal_generator]") {
    SignalConfig cfg;
    cfg.amplitude = 0.0;
    cfg.frequency_hz = 1.0;
    cfg.phase_rad = 0.0;
    cfg.offset = 5.0;
    cfg.noise_stddev = 0.0;

    SignalGenerator gen(cfg);

    // Any timestamp should return exactly offset / 任意时间戳都应返回 offset
    double v0 = gen.value(0);
    REQUIRE_THAT(v0, Catch::Matchers::WithinAbs(5.0, kEps));

    double v1 = gen.value(123'456'789);
    REQUIRE_THAT(v1, Catch::Matchers::WithinAbs(5.0, kEps));

    double v2 = gen.value(9'999'999'999'999'999);
    REQUIRE_THAT(v2, Catch::Matchers::WithinAbs(5.0, kEps));
}

// ---------------------------------------------------------------------------
// 5. Noise statistics — mean ≈ 0 / 噪声统计——均值 ≈ 0
//    Fixed t, A=0, offset=0, σ=1.0, N=100k samples.
//    固定 t, A=0, offset=0, σ=1.0, N=100k 采样。
//    Sample mean should be within 3 * σ/√N ≈ 0.0095.
//    样本均值应在 3 * σ/√N ≈ 0.0095 范围内。
// ---------------------------------------------------------------------------

TEST_CASE("SignalGenerator noise -- mean ~ 0", "[core][signal_generator][noise]") {
    SignalConfig cfg;
    cfg.amplitude = 0.0;
    cfg.offset = 0.0;
    cfg.noise_stddev = 1.0;
    cfg.seed = 42u;

    SignalGenerator gen(cfg);

    constexpr std::int64_t t_fixed = 1'000'000'000; // 1s / 1 秒
    double sum = 0.0;
    for (int i = 0; i < kStatsN; ++i) {
        sum += gen.value(t_fixed);
    }
    double mean = sum / kStatsN;

    // 3σ tolerance: 3 * σ / √N = 3 * 1 / 316.23 ≈ 0.0095 → use 0.01
    // 3σ 容差:3 * σ / √N ≈ 0.0095 → 使用 0.01
    REQUIRE_THAT(mean, Catch::Matchers::WithinAbs(0.0, 0.01));
}

// ---------------------------------------------------------------------------
// 6. Noise statistics — stddev ≈ σ / 噪声统计——标准差 ≈ σ
//    Same 100k samples: sample stddev ≈ 1.0.
//    同 100k 采样:样本标准差 ≈ 1.0。
// ---------------------------------------------------------------------------

TEST_CASE("SignalGenerator noise -- stddev ~ sigma", "[core][signal_generator][noise]") {
    SignalConfig cfg;
    cfg.amplitude = 0.0;
    cfg.offset = 0.0;
    cfg.noise_stddev = 1.0;
    cfg.seed = 42u;

    SignalGenerator gen(cfg);

    constexpr std::int64_t t_fixed = 1'000'000'000;
    double sum = 0.0;
    double sum_sq = 0.0;
    for (int i = 0; i < kStatsN; ++i) {
        double v = gen.value(t_fixed);
        sum += v;
        sum_sq += v * v;
    }
    double mean = sum / kStatsN;
    double variance = sum_sq / kStatsN - mean * mean;
    double stddev = std::sqrt(variance);

    // 3σ tolerance for sample stddev ≈ 0.007 → use 0.01
    // 样本标准差的 3σ 容差 ≈ 0.007 → 使用 0.01
    REQUIRE_THAT(stddev, Catch::Matchers::WithinAbs(1.0, 0.01));
}

// ---------------------------------------------------------------------------
// 7. Reproducibility — same seed, same output / 可复现——同种子,同输出
//    Two instances with identical seed & config must produce identical
//    sequences.  Uses noise_stddev > 0 so RNG is actually exercised.
//    两个同种子同配置的实例必须产出相同序列。
//    使用 noise_stddev > 0 实际调用 RNG。
// ---------------------------------------------------------------------------

TEST_CASE("SignalGenerator reproducibility -- same seed", "[core][signal_generator]") {
    SignalConfig cfg;
    cfg.amplitude = 1.0;
    cfg.frequency_hz = 10.0;
    cfg.phase_rad = 0.5;
    cfg.offset = 0.2;
    cfg.noise_stddev = 1.0; // > 0 so RNG is exercised / > 0 使 RNG 实际参与
    cfg.seed = 12345u;

    SignalGenerator gen1(cfg);
    SignalGenerator gen2(cfg);

    // Feed identical timestamp sequences; expect point-by-point equality.
    // 输入相同时间戳序列;期望逐点相等。
    for (std::int64_t t = 0; t < 1'000'000'000; t += 100'000'000) {
        double v1 = gen1.value(t);
        double v2 = gen2.value(t);
        REQUIRE_THAT(v1, Catch::Matchers::WithinAbs(v2, kEps));
    }
}

// ---------------------------------------------------------------------------
// 8. Reproducibility — reset() restores sequence / 可复现——reset() 恢复序列
//    Call value() N times, reset(), call value() N times again — outputs
//    must match.  Uses noise_stddev > 0.
//    调用 value() N 次,reset(),再调用 value() N 次——输出必须匹配。
//    使用 noise_stddev > 0。
// ---------------------------------------------------------------------------

TEST_CASE("SignalGenerator reset() restores original sequence", "[core][signal_generator]") {
    SignalConfig cfg;
    cfg.amplitude = 1.0;
    cfg.frequency_hz = 10.0;
    cfg.noise_stddev = 1.0; // > 0 so reset() matters / > 0 使 reset() 有意义
    cfg.seed = 99999u;

    SignalGenerator gen(cfg);

    constexpr int N = 100;
    double first_pass[N];

    for (int i = 0; i < N; ++i) {
        first_pass[i] = gen.value(i * 10'000'000LL);
    }

    // Reset and re-generate — must match / 重置后重新生成——必须匹配
    gen.reset();
    for (int i = 0; i < N; ++i) {
        double v = gen.value(i * 10'000'000LL);
        REQUIRE_THAT(v, Catch::Matchers::WithinAbs(first_pass[i], kEps));
    }
}

// ---------------------------------------------------------------------------
// 9. Seed sensitivity — different seeds produce different output
//    种子敏感——不同种子产生不同输出。
//    Uses noise_stddev > 0; same timestamps, different seeds ⇒ values differ.
//    使用 noise_stddev > 0;相同时间戳,不同种子 ⇒ 值不同。
// ---------------------------------------------------------------------------

TEST_CASE("SignalGenerator seed sensitivity", "[core][signal_generator]") {
    SignalConfig cfg_a;
    cfg_a.amplitude = 1.0;
    cfg_a.frequency_hz = 10.0;
    cfg_a.noise_stddev = 1.0; // > 0 required for seed to matter / > 0 种子才会产生影响
    cfg_a.seed = 11111u;

    SignalConfig cfg_b = cfg_a;
    cfg_b.seed = 22222u;

    SignalGenerator gen_a(cfg_a);
    SignalGenerator gen_b(cfg_b);

    // The sine component is identical; noise differs → first value should differ.
    // 正弦分量相同;噪声不同 → 第一个值应不同。
    double va = gen_a.value(0);
    double vb = gen_b.value(0);
    REQUIRE(va != vb);
}

// ---------------------------------------------------------------------------
// 10. Config mutation via set_config / 通过 set_config 修改配置
//     Changing amplitude mid-stream takes effect immediately.
//     中途修改 amplitude 立即生效。
// ---------------------------------------------------------------------------

TEST_CASE("SignalGenerator set_config takes effect immediately", "[core][signal_generator]") {
    SignalConfig cfg;
    cfg.amplitude = 1.0;
    cfg.frequency_hz = 1.0;
    cfg.phase_rad = 0.0;
    cfg.offset = 0.0;
    cfg.noise_stddev = 0.0;

    SignalGenerator gen(cfg);

    // Original: A=1 → sin(π/2)=1 at t=0.25s / 原始:A=1 → t=0.25s 处 sin(π/2)=1
    double v = gen.value(250'000'000);
    REQUIRE_THAT(v, Catch::Matchers::WithinAbs(1.0, kEps));

    // Change amplitude to 3.0 / 将幅值改为 3.0
    cfg.amplitude = 3.0;
    gen.set_config(cfg);

    // Now: A=3 → 3 * sin(π/2) = 3.0 at same t / 现在:A=3 → 同 t 处 3 * sin(π/2) = 3.0
    v = gen.value(250'000'000);
    REQUIRE_THAT(v, Catch::Matchers::WithinAbs(3.0, kEps));
}

// ---------------------------------------------------------------------------
// 11. σ=0 → RNG never touched (deterministic pure sine)
//     σ=0 → 绝不碰 RNG (确定性纯正弦)
//     Verify that value() is deterministic across consecutive calls after reset.
//     验证 reset 后连续调用 value() 确定性一致。
// ---------------------------------------------------------------------------

TEST_CASE("SignalGenerator σ=0 is purely deterministic", "[core][signal_generator]") {
    SignalConfig cfg;
    cfg.amplitude = 2.5;
    cfg.frequency_hz = 5.0;
    cfg.phase_rad = 0.3;
    cfg.offset = 1.5;
    cfg.noise_stddev = 0.0;
    cfg.seed = 0u; // irrelevant when σ=0, but set it anyway / σ=0 时无关,但照样设

    SignalGenerator gen(cfg);

    constexpr int N = 50;
    double first_pass[N];

    for (int i = 0; i < N; ++i) {
        first_pass[i] = gen.value(i * 5'000'000LL);
    }

    // reset() + re-generate: must match exactly even with different seed values
    // reset() + 重新生成:即使换种子也应完全一致
    gen.reset();
    for (int i = 0; i < N; ++i) {
        double v = gen.value(i * 5'000'000LL);
        REQUIRE_THAT(v, Catch::Matchers::WithinAbs(first_pass[i], kEps));
    }
}
