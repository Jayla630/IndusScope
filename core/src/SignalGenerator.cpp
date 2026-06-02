#include "indusscope/core/SignalGenerator.h"

#include <cmath>

namespace indusscope::core {

// 2π as constexpr; avoids M_PI (non-standard) and std::numbers (C++20).
// 2π 用 constexpr;避免 M_PI (非标准) 和 std::numbers (C++20)。
constexpr double kTwoPi = 6.283185307179586476925286766559;

SignalGenerator::SignalGenerator(const SignalConfig& config)
    : m_config(config)
    , m_original_seed(config.seed)
    , m_rng(config.seed)
{
    m_noise.reset();
}

double SignalGenerator::value(std::int64_t timestamp_ns) {
    // Convert nanoseconds to seconds / 纳秒转秒
    const double t = static_cast<double>(timestamp_ns) * 1e-9;

    // Pure sine + DC offset / 纯正弦 + 直流偏置
    double result = m_config.amplitude *
                        std::sin(kTwoPi * m_config.frequency_hz * t + m_config.phase_rad) +
                    m_config.offset;

    // Gaussian noise: standard normal × σ; σ==0 skips RNG entirely.
    // 高斯噪声:标准正态 × σ;σ==0 完全跳过 RNG。
    if (m_config.noise_stddev > 0.0) {
        result += m_noise(m_rng) * m_config.noise_stddev;
    }

    return result;
}

void SignalGenerator::reseed(std::uint_fast32_t seed) {
    m_rng.seed(seed);
    m_noise.reset(); // clear Box-Muller cached spare / 清除 Box-Muller 缓存的备用值
}

void SignalGenerator::reset() {
    m_rng.seed(m_original_seed);
    m_noise.reset(); // clear Box-Muller cached spare / 清除 Box-Muller 缓存的备用值
}

void SignalGenerator::set_config(const SignalConfig& config) {
    m_config = config;
    m_original_seed = config.seed;
    m_rng.seed(config.seed);
    m_noise.reset(); // clear Box-Muller cached spare / 清除 Box-Muller 缓存的备用值
}

} // namespace indusscope::core
