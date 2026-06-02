#pragma once

#include <cstdint>
#include <random>

namespace indusscope::core {

/// Configuration for the signal generator.
/// 信号发生器配置。
struct SignalConfig {
    /// Sine amplitude.
    /// 正弦幅值。
    double amplitude{1.0};

    /// Signal frequency in Hz (NOT sampling rate).
    /// 信号频率 (Hz),非采样率。
    double frequency_hz{1.0};

    /// Initial phase in radians.
    /// 初始相位 (弧度)。
    double phase_rad{0.0};

    /// DC offset added after sine + noise.
    /// 加在正弦 + 噪声之后的直流偏置。
    double offset{0.0};

    /// Gaussian noise standard deviation; σ=0 ⇒ deterministic pure sine.
    /// 高斯噪声标准差;σ=0 则输出确定性的纯正弦。
    double noise_stddev{0.0};

    /// RNG seed (mt19937 default: 5489u).
    /// 随机数种子 (mt19937 默认值: 5489u)。
    std::uint_fast32_t seed{5489u};
};

/// Pure-function signal generator: sine + Gaussian noise + DC offset.
/// 纯函数信号发生器:正弦 + 高斯噪声 + 直流偏置。
///
/// Given a timestamp, computes the signal value deterministically
/// (modulo RNG state for noise).  No timing, no buffering, no threading —
/// just the math.  Designed as the signal source for the timed sampling
/// loop in S1.3b.
/// 给定时间戳,确定性计算信号值 (噪声部分依赖 RNG 状态)。无时序、无缓冲、
/// 无线程——只做数学。设计为 S1.3b 定时采样循环的信号源。
///
/// Noise: uses a standard normal distribution member (μ=0, σ=1) and
/// scales by config.noise_stddev.  When noise_stddev == 0 the RNG is
/// not touched at all — value() is purely deterministic.
/// 噪声:使用标准正态分布成员 (μ=0, σ=1),再乘以 config.noise_stddev。
/// noise_stddev == 0 时完全不碰 RNG——value() 完全确定性。
class SignalGenerator {
public:
    /// Construct with optional config; seeds RNG, resets noise distribution.
    /// 用可选配置构造;播种 RNG,重置噪声分布状态。
    explicit SignalGenerator(const SignalConfig& config = SignalConfig{});

    /// Compute signal value at timestamp @p timestamp_ns (nanoseconds).
    /// 计算 @p timestamp_ns (纳秒) 时刻的信号值。
    ///
    /// Returns: A·sin(2π·f·t + φ) + offset + N(0, σ)
    ///   where t = timestamp_ns × 1e-9
    ///   其中 t = timestamp_ns × 1e-9
    ///
    /// When noise_stddev == 0 the RNG is not invoked (pure deterministic).
    /// noise_stddev == 0 时不调用 RNG (完全确定性)。
    double value(std::int64_t timestamp_ns);

    /// Re-seed the RNG and reset noise distribution state.
    /// 重新播种 RNG 并重置噪声分布状态。
    void reseed(std::uint_fast32_t seed);

    /// Reset RNG to the original seed; restores initial noise state.
    /// 将 RNG 重置为原始种子;恢复初始噪声状态。
    /// Equivalent to reseed(config().seed).
    /// 等效于 reseed(config().seed)。
    void reset();

    /// Read-only access to current config.
    /// 当前配置的只读访问。
    const SignalConfig& config() const noexcept { return m_config; }

    /// Replace config, re-seed RNG, and reset noise distribution state.
    /// 替换配置,重新播种 RNG,重置噪声分布状态。
    /// The noise distribution itself is not rebuilt (stays standard normal).
    /// 不重建噪声分布对象 (保持标准正态)。
    void set_config(const SignalConfig& config);

private:
    SignalConfig m_config;
    std::uint_fast32_t m_original_seed; // saved for reset() / 保存供 reset() 使用

    // RNG engine: Mersenne Twister 19937 / 梅森旋转引擎
    std::mt19937 m_rng;

    // Standard normal (μ=0, σ=1); scaled by noise_stddev in value().
    // 标准正态分布 (μ=0, σ=1);在 value() 中乘以 noise_stddev 缩放。
    std::normal_distribution<double> m_noise{0.0, 1.0};
};

} // namespace indusscope::core
