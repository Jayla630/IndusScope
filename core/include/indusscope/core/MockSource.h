#pragma once

#include "indusscope/core/RingBuffer.h"
#include "indusscope/core/SamplePoint.h"
#include "indusscope/core/SignalGenerator.h"

#include <chrono>
#include <cstdint>

namespace indusscope::core {

/// Configuration for MockSource.
/// MockSource 配置。
struct MockSourceConfig {
    /// Target sampling rate in Hz (NOT signal frequency).
    /// 目标采样率 (Hz),非信号频率。
    /// Must be > 0; non-positive values are clamped in constructor.
    /// 必须 > 0;非正值在构造时 clamp。
    double sample_rate_hz{1000.0};

    /// Timestamp of the 0-th sample (nanoseconds).
    /// 第 0 个样本的时间戳 (纳秒)。
    std::int64_t start_timestamp_ns{0};

    /// Signal generator configuration.
    /// 信号发生器配置。
    SignalConfig signal_config{};
};

/// Statistics returned by run_for().
/// run_for() 返回的统计数据。
struct RunStats {
    /// Samples successfully pushed to sink.
    /// 成功推入 sink 的样本数。
    std::size_t produced{0};

    /// Samples rejected because sink was full.
    /// 因 sink 满被拒绝的样本数。
    std::size_t dropped{0};

    /// Wall-clock elapsed in nanoseconds.
    /// 实际经过的 wall-clock 纳秒数。
    std::int64_t elapsed_ns{0};

    /// Achieved rate = produced / elapsed_seconds; 0.0 if none produced.
    /// 实际产出率 = produced / 经过秒数;无产出时为 0.0。
    double achieved_rate_hz{0.0};
};

/// Paced signal sampler: generates samples on a logical timestamp grid
/// and pushes them into an externally-owned RingBuffer<SamplePoint>.
/// 定时信号采样器:在逻辑时间戳网格上生成样本,推入外部持有的 RingBuffer<SamplePoint>。
///
/// Two entry points / 两个入口:
/// - produce(n): deterministic pump, no sleep — for unit tests.
///   produce(n): 确定性泵,不睡觉——给单测用。
/// - run_for(duration): wall-clock paced on an absolute sleep grid — for demos.
///   run_for(duration): 按绝对 sleep 网格跑 wall-clock——给 demo 用。
///
/// Key design — timestamp grid decoupled from wall clock:
/// 核心设计——时间戳网格与 wall-clock 解耦:
///   ts_i = start_timestamp_ns + i * period_ns
/// The logical grid is fixed ("train schedule"); wall-clock jitter only
/// affects when samples are emitted, never their timestamps or values.
/// 逻辑网格固定 ("火车时刻表");wall-clock 抖动只影响"何时发车",
/// 不影响样本的时间戳或值。Therefore produce(k) and the first k samples
/// from run_for() are point-by-point identical (when σ=0).
/// 因此 produce(k) 与 run_for() 的前 k 个样本逐点相同 (σ=0 时)。
///
/// Single-threaded by design; run_for() blocks the calling thread.
/// S2.1 will move the loop to a worker thread.
/// 设计为单线程;run_for() 阻塞调用线程。S2.1 将把循环搬到 worker 线程。
class MockSource {
public:
    /// Construct with config and an external RingBuffer reference (not owned).
    /// 用配置和外部 RingBuffer 引用 (不拥有) 构造。
    /// @pre sample_rate_hz > 0 (clamped internally if not).
    ///      前置:sample_rate_hz > 0 (否则内部 clamp)。
    MockSource(const MockSourceConfig& cfg, RingBuffer<SamplePoint>& sink);

    // --- Produce 产出 ---

    /// Deterministic pump: emit exactly @p n samples, no sleep.
    /// 确定性泵:恰好产出 @p n 个样本,不睡觉。
    void produce(std::size_t n);

    /// Wall-clock paced run for ~@p duration.
    /// 按 wall-clock 跑约 @p duration 时长。
    /// Sleeps on an absolute grid (sleep_until); if behind schedule,
    /// sleep_until returns immediately to catch up.
    /// 在绝对网格上 sleep (sleep_until);落后于时刻表时立即返回追平。
    /// Returns per-run statistics (produced, dropped, elapsed, rate).
    /// 返回本次运行的统计数据。
    RunStats run_for(std::chrono::nanoseconds duration);

    // --- Metrics 指标 ---

    /// Cumulative samples successfully pushed.
    /// 累计成功推入数。
    std::size_t produced() const noexcept { return m_produced; }

    /// Cumulative samples rejected (sink full).
    /// 累计被拒数 (sink 满)。
    std::size_t dropped()  const noexcept { return m_dropped; }

    /// Monotonic emit counter (total attempts = produced + dropped).
    /// 单调发射计数 (总尝试 = produced + dropped)。
    std::size_t index()    const noexcept { return m_index; }

    // --- Config 配置 ---

    /// Read-only config access.
    /// 只读配置访问。
    const MockSourceConfig& config() const noexcept { return m_config; }

    /// Replace config and fully reset state (counters, generator, period).
    /// 替换配置并全量重置状态 (计数器、发生器、周期)。
    /// After this call: index()==0, produced()==0, dropped()==0,
    /// generator RNG re-seeded with fresh signal_config.seed.
    /// 调用后:index()==0, produced()==0, dropped()==0,
    /// 发生器 RNG 用给定 signal_config.seed 重新播种。
    void set_config(const MockSourceConfig& cfg);

    /// Zero counters and reset generator RNG to original seed.
    /// 清零计数器并将发生器 RNG 重置为原始种子。
    /// Does NOT change config.  不修改配置。
    void reset();

private:
    /// Emit one sample to sink.
    /// 发射一个样本到 sink。
    /// Timestamp = m_config.start_timestamp_ns + m_index * m_period_ns.
    /// 时间戳 = m_config.start_timestamp_ns + m_index * m_period_ns。
    /// Value = m_generator.value(timestamp).
    /// 值 = m_generator.value(timestamp)。
    /// On push failure: ++m_dropped; on success: ++m_produced.
    /// push 失败:++m_dropped;成功:++m_produced。
    /// Always increments m_index.  始终递增 m_index。
    void emit_one();

    MockSourceConfig m_config;
    RingBuffer<SamplePoint>& m_sink; // external, not owned / 外部持有
    SignalGenerator m_generator;

    std::int64_t m_period_ns; // = max(1, llround(1e9 / sample_rate_hz)) / 采样周期间隔 (纳秒)

    std::size_t m_produced{0};
    std::size_t m_dropped{0};
    std::size_t m_index{0};   // monotonic emit counter → drives timestamp grid / 单调发射计数 → 驱动时间戳网格
};

} // namespace indusscope::core
