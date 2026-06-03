#pragma once

#include <QObject>

#include "indusscope/core/MockSource.h"  // m_source is a value member / m_source 是值成员

class QTimer;

namespace indusscope::ui {

/// Producer half of the sampling pipeline — owns MockSource + QTimer,
/// pushes samples into an externally-owned RingBuffer.
/// 采样管线的生产者半场——持有 MockSource + QTimer,将样本推入外部持有的 RingBuffer。
///
/// Single-threaded in this slice (timer runs on caller's thread);
/// S2.1b will move the timer to a dedicated worker thread.
/// 本 slice 为单线程 (timer 跑在调用方线程);S2.1b 将把 timer 移到专用 worker 线程。
///
/// Only calls RingBuffer::push() (via MockSource::produce()) — never pop.
/// 仅调用 RingBuffer::push() (通过 MockSource::produce())——绝不 pop。
///
/// Not a QML_ELEMENT — plain QObject with Q_OBJECT for moc.
/// 非 QML_ELEMENT——带 Q_OBJECT 的纯 QObject,走 moc 即可。
class AcquisitionWorker : public QObject {
    Q_OBJECT

public:
    /// Construct with an external RingBuffer reference and MockSource config.
    /// 用外部 RingBuffer 引用和 MockSource 配置构造。
    /// @param sink  Ring buffer to push samples into (not owned).
    ///              sink  样本推入的环形缓冲 (不拥有所有权)。
    /// @param cfg   MockSource configuration (sample rate, signal, noise).
    ///              cfg   MockSource 配置 (采样率、信号、噪声)。
    /// @param parent Qt parent for lifetime management.
    ///              parent  Qt 父对象,用于生命周期管理。
    explicit AcquisitionWorker(
        indusscope::core::RingBuffer<indusscope::core::SamplePoint>& sink,
        const indusscope::core::MockSourceConfig& cfg,
        QObject* parent = nullptr
    );

public slots:
    /// Start the produce timer.  Idempotent — no-op if already running.
    /// 启动 produce 定时器。幂等——已运行时为空操作。
    void start();

    /// Stop the produce timer.  Idempotent — no-op if already stopped.
    /// 停止 produce 定时器。幂等——已停止时为空操作。
    void stop();

signals:
    /// Emitted when the produce timer starts.
    /// produce 定时器启动时发射。
    void started();

    /// Emitted when the produce timer stops.
    /// produce 定时器停止时发射。
    void stopped();

private slots:
    /// Timer callback — calls m_source.produce(kProducePerTick).
    /// 定时器回调——调用 m_source.produce(kProducePerTick)。
    void onProduceTick();

private:
    // --- Constants 常量 ---
    // 16 ms interval × 80 points = 5000 Hz logical sample rate.
    // 16 ms 间隔 × 80 点 = 5000 Hz 逻辑采样率。
    static constexpr int kTimerIntervalMs = 16;
    static constexpr int kProducePerTick  = 80;

    // --- Producer 生产者 ---

    /// Mock signal source — deterministic pump, no sleep.
    /// 模拟信号源——确定性泵,不睡觉。
    /// Constructed in-place in init list with (cfg, sink); not movable.
    /// 在初始化列表中用 (cfg, sink) 就地构造;不可移动。
    indusscope::core::MockSource m_source;

    /// Timer driving the produce cycle.  Owned by Qt parent-child (this).
    /// 驱动 produce 循环的定时器。由 Qt 父子关系持有 (this)。
    QTimer* m_produceTimer = nullptr;

    bool m_running = false;
};

} // namespace indusscope::ui
