#pragma once

#include "indusscope/core/RingBuffer.h"
#include "indusscope/core/SamplePoint.h"
#include "indusscope/protocol/IDeviceProtocol.h"
#include "indusscope/protocol/ProtocolConfig.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace indusscope::core {

/// Adapts an IDeviceProtocol into the core sampling pipeline.
/// 将 IDeviceProtocol 适配进 core 采样管线。
///
/// Polls the protocol for Readings, filters to a single target channel,
/// translates each matching Reading to a SamplePoint, and pushes into
/// an externally-owned RingBuffer.
/// 轮询协议获取 Reading,过滤到单个目标通道,将匹配 Reading 译成
/// SamplePoint 后推入外部持有的 RingBuffer。
///
/// Pure C++ — no QObject, no Q_OBJECT, no signals, no thread affinity.
/// All methods (start/acquire/stop) must be called only from the worker thread.
/// 纯 C++——无 QObject、无 Q_OBJECT、无信号、无线程亲和。
/// 所有方法 (start/acquire/stop) 必须仅在 worker 线程调用。
class ProtocolSource {
public:
    /// @param proto   Owning handle to a protocol instance (moved in).
    ///                协议实例所有权移入。
    /// @param sink    External ring buffer, not owned.
    ///                外部持有的环形缓冲,不拥有所有权。
    /// @param cfg     Configuration forwarded verbatim to proto->configure().
    ///                透传给 proto->configure() 的配置。
    /// @param channel Target logical channel index; only Readings with this channel are pushed.
    ///                目标逻辑通道号;仅具有此通道号的 Reading 才会被推入。
    ProtocolSource(
        std::unique_ptr<indusscope::protocol::IDeviceProtocol> proto,
        RingBuffer<SamplePoint>& sink,
        const indusscope::protocol::ProtocolConfig& cfg,
        std::uint32_t channel = 0
    );

    /// configure() then open(). Returns false and leaves protocol closed on open() failure.
    /// configure() 再 open()。open() 失败时返回 false,协议保持关闭状态。
    bool start();

    /// Poll up to max_n Readings, filter to target channel, push matching ones as SamplePoints.
    /// 轮询最多 max_n 个 Reading,过滤目标通道后将匹配项以 SamplePoint 推入环缓冲。
    ///
    /// max_n is clamped to kMaxPoll (256); call acquire() multiple times for larger batches.
    /// max_n 受内部 scratch 容量 kMaxPoll (256) 上限钳制;更大批次请多次调用 acquire()。
    ///
    /// Returns the count of SamplePoints successfully pushed. Returns 0 when not started.
    /// 返回成功推入的 SamplePoint 数。未 start 时返回 0。
    std::size_t acquire(std::size_t max_n);

    /// close() the protocol.
    /// 关闭协议。
    void stop();

    // --- Metrics 指标 ---

    /// Cumulative SamplePoints successfully pushed to the ring buffer.
    /// 累计成功推入环缓冲的 SamplePoint 数。
    std::size_t produced() const noexcept { return m_produced; }

    /// Cumulative SamplePoints rejected because the ring buffer was full.
    /// 因环缓冲已满而被拒绝的 SamplePoint 累计数。
    std::size_t dropped()  const noexcept { return m_dropped; }

private:
    /// Internal scratch capacity — avoids VLAs and heap allocation in the hot path.
    /// 内部 scratch 容量——避免热路径上的 VLA 和堆分配。
    static constexpr std::size_t kMaxPoll = 256;

    std::unique_ptr<indusscope::protocol::IDeviceProtocol> m_proto;
    RingBuffer<SamplePoint>& m_sink;
    indusscope::protocol::ProtocolConfig m_cfg;
    std::uint32_t m_channel;
    std::size_t m_produced{0};
    std::size_t m_dropped{0};
};

} // namespace indusscope::core
