#pragma once

#include <QObject>
#include <QString>

#include "indusscope/core/ProtocolSource.h"  // m_source is a value member / m_source 是值成员
#include "indusscope/protocol/IDeviceProtocol.h"
#include "indusscope/protocol/ProtocolConfig.h"

#include <cstdint>
#include <memory>

class QTimer;

namespace indusscope::ui {

/// Producer half of the sampling pipeline — owns ProtocolSource + QTimer,
/// pushes samples into an externally-owned RingBuffer.
/// 采样管线的生产者半场——持有 ProtocolSource + QTimer,将样本推入外部持有的 RingBuffer。
///
/// configure()/open()/poll()/close() are called only from the worker thread
/// (start/onProduceTick/stop slots run via QueuedConnection from the worker thread).
/// configure()/open()/poll()/close() 仅在 worker 线程调用
/// (start/onProduceTick/stop 槽通过 QueuedConnection 在 worker 线程运行)。
///
/// Not a QML_ELEMENT — plain QObject with Q_OBJECT for moc.
/// 非 QML_ELEMENT——带 Q_OBJECT 的纯 QObject,走 moc 即可。
class AcquisitionWorker : public QObject {
    Q_OBJECT

public:
    /// Construct with an already-created protocol instance, ring buffer, config, and channel.
    /// 用已创建的协议实例、环形缓冲、配置和通道索引构造。
    /// @param proto   Protocol instance (ownership transferred).
    ///                协议实例 (所有权移入)。
    /// @param sink    Ring buffer to push samples into (not owned).
    ///                样本推入的环形缓冲 (不拥有所有权)。
    /// @param cfg     ProtocolConfig forwarded verbatim to ProtocolSource.
    ///                透传给 ProtocolSource 的 ProtocolConfig。
    /// @param channel Target logical channel index.
    ///                目标逻辑通道号。
    /// @param parent  Qt parent for lifetime management.
    ///                Qt 父对象,用于生命周期管理。
    explicit AcquisitionWorker(
        std::unique_ptr<indusscope::protocol::IDeviceProtocol> proto,
        indusscope::core::RingBuffer<indusscope::core::SamplePoint>& sink,
        const indusscope::protocol::ProtocolConfig& cfg,
        std::uint32_t channel,
        QObject* parent = nullptr
    );

public slots:
    /// Start the produce timer.  Calls m_source.start() (configure+open) in worker thread.
    /// Idempotent — no-op if already running.  open() failure emits error() and bails.
    /// 启动 produce 定时器。在 worker 线程调用 m_source.start() (configure+open)。
    /// 幂等——已运行时为空操作。open() 失败时发射 error() 并提前返回。
    void start();

    /// Stop the produce timer and close the protocol.  Idempotent — no-op if already stopped.
    /// 停止 produce 定时器并关闭协议。幂等——已停止时为空操作。
    void stop();

signals:
    /// Emitted when the produce timer starts successfully.
    /// produce 定时器成功启动时发射。
    void started();

    /// Emitted when the produce timer stops.
    /// produce 定时器停止时发射。
    void stopped();

    /// Emitted when the protocol fails to open; timer is NOT started.
    /// 协议 open() 失败时发射;定时器不会启动。
    void error(const QString& message);

private slots:
    /// Timer callback — calls m_source.acquire(kProducePerTick).
    /// 定时器回调——调用 m_source.acquire(kProducePerTick)。
    void onProduceTick();

private:
    // --- Constants 常量 ---
    // 16 ms interval × 80 points = 5000 Hz logical sample rate.
    // 16 ms 间隔 × 80 点 = 5000 Hz 逻辑采样率。
    static constexpr int kTimerIntervalMs = 16;
    static constexpr int kProducePerTick  = 80;

    // --- Producer 生产者 ---

    /// Protocol-backed sample source — poll→filter→translate→push.
    /// 协议驱动的样本源——poll→过滤→转译→push。
    /// Constructed in-place in init list; not movable after construction.
    /// 在初始化列表中就地构造;构造后不可移动。
    indusscope::core::ProtocolSource m_source;

    /// Timer driving the produce cycle.  Owned by Qt parent-child (this).
    /// 驱动 produce 循环的定时器。由 Qt 父子关系持有 (this)。
    QTimer* m_produceTimer = nullptr;

    bool m_running = false;

    /// One-shot guard for first-tick thread-id log — avoids log spam.
    /// 首 tick 线程 id 日志的一次性守卫——避免刷屏。
    bool m_loggedThread = false;
};

} // namespace indusscope::ui
