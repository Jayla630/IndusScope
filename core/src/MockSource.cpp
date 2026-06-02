#include "indusscope/core/MockSource.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace indusscope::core {

// --- Helpers 辅助函数 ---

/// Derive period_ns from sample_rate_hz with a floor of 1 ns.
/// 从 sample_rate_hz 推导 period_ns,下限 1 ns。
/// Clamps non-positive rates to 1.0 Hz to avoid 0-period spin / Inf.
/// 非正值 clamp 到 1.0 Hz,防止 0 周期空转 / Inf。
static std::int64_t derive_period_ns(double sample_rate_hz) {
    if (sample_rate_hz <= 0.0) {
        sample_rate_hz = 1.0; // safe floor / 安全下限
    }
    auto period = static_cast<std::int64_t>(
        std::llround(1'000'000'000.0 / sample_rate_hz));
    return std::max<std::int64_t>(1, period);
}

// --- MockSource 实现 ---

MockSource::MockSource(const MockSourceConfig& cfg, RingBuffer<SamplePoint>& sink)
    : m_config(cfg)
    , m_sink(sink)
    , m_generator(cfg.signal_config)
    , m_period_ns(derive_period_ns(cfg.sample_rate_hz))
{
}

void MockSource::emit_one() {
    // Logical timestamp grid: ts_i = start + i * period (pure integer, no drift).
    // 逻辑时间戳网格:ts_i = start + i * period (纯整数,无累积误差)。
    std::int64_t ts = m_config.start_timestamp_ns +
                      static_cast<std::int64_t>(m_index) * m_period_ns;

    double v = m_generator.value(ts);
    SamplePoint sp{ts, v};

    if (m_sink.push(sp)) {
        ++m_produced;
    } else {
        ++m_dropped;
    }
    ++m_index;
}

void MockSource::produce(std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        emit_one();
    }
}

RunStats MockSource::run_for(std::chrono::nanoseconds duration) {
    using clock = std::chrono::steady_clock;

    auto start_wall = clock::now();
    auto deadline = start_wall + duration;
    auto next_wake = start_wall; // first tick fires immediately / 第一个 tick 立即触发

    std::size_t prod_before = m_produced;
    std::size_t drop_before = m_dropped;

    while (clock::now() < deadline) {
        // sleep_until absolute grid: if behind, returns immediately (catch-up).
        // sleep_until 绝对网格:落后则立即返回 (追平)。
        std::this_thread::sleep_until(next_wake);

        // Re-check deadline after waking — sleep may have overshot.
        // 醒后再检查截止时间——sleep 可能超调。
        if (clock::now() >= deadline) {
            break;
        }

        emit_one();
        next_wake += std::chrono::nanoseconds(m_period_ns);
    }

    auto end_wall = clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end_wall - start_wall);

    RunStats stats;
    stats.produced = m_produced - prod_before;
    stats.dropped  = m_dropped - drop_before;
    stats.elapsed_ns = elapsed.count();

    if (stats.produced > 0 && stats.elapsed_ns > 0) {
        double elapsed_s = static_cast<double>(stats.elapsed_ns) * 1e-9;
        stats.achieved_rate_hz = static_cast<double>(stats.produced) / elapsed_s;
    }
    // else: achieved_rate_hz stays 0.0 / 否则保持 0.0

    return stats;
}

void MockSource::set_config(const MockSourceConfig& cfg) {
    m_config = cfg;
    m_period_ns = derive_period_ns(cfg.sample_rate_hz);
    m_generator = SignalGenerator(cfg.signal_config); // full reset, fresh seed / 全量重置
    m_produced = 0;
    m_dropped  = 0;
    m_index    = 0;
}

void MockSource::reset() {
    m_generator.reset(); // restore original seed, clear Box-Muller / 恢复原始种子,清 Box-Muller
    m_produced = 0;
    m_dropped  = 0;
    m_index    = 0;
}

} // namespace indusscope::core
