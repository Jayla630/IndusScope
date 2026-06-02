#pragma once

#include <QObject>
#include <QList>
#include <QPointF>
#include <QtQml/qqmlregistration.h>

#include <deque>
#include <memory>
#include <cstdint>

// Forward-declare core types to keep header free of core implementation details.
// Core types are fully included only in the .cpp translation unit.
// 前置声明 core 类型,头文件不引入 core 实现细节。core 类型仅在 .cpp 中完整包含。
namespace indusscope::core {
struct SamplePoint;
template <typename T> class RingBuffer;
class MockSource;
struct MockSourceConfig;
} // namespace indusscope::core

class QTimer;

namespace indusscope::ui {

/// Renderer-agnostic scrolling curve adapter — driven by QTimer + MockSource.
/// 渲染器无关的滚动曲线适配器——由 QTimer + MockSource 驱动。
///
/// Each timer tick:
///   1. MockSource.produce(k) → RingBuffer
///   2. RingBuffer.pop_batch() → fixed-capacity deque window
///   3. Trim window to kWindowSize (oldest points evicted)
///   4. Snapshot to QList<QPointF>, update xMin/xMax, emit signals
/// 每次 timer 滴答:
///   1. MockSource.produce(k) → 环形缓冲
///   2. RingBuffer.pop_batch() → 固定容量 deque 窗口
///   3. 裁剪窗口至 kWindowSize (老点被挤出)
///   4. 快照到 QList<QPointF>,更新 xMin/xMax,发射信号
///
/// Does NOT #include or link any chart library — the only dependencies are
/// Qt6::Core (QObject/QTimer/signals) + Qt6::Qml (QML_ELEMENT) + Qt::Gui (QPointF)
/// + indusscope::core (RingBuffer/MockSource/SamplePoint/SignalGenerator).
/// 不 #include 或链接任何图表库——仅依赖 Qt6::Core + Qt6::Qml + Qt::Gui + core。
///
/// Construction only assembles members; QML must call start() explicitly
/// to begin the data pump — no hidden side effects in the constructor.
/// 构造仅装配成员;QML 须显式调用 start() 启动数据泵——构造函数无隐藏副作用。
class CurveController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// Snapshot of the scrolling window as QPointF list.
    /// X = time in seconds, Y = signal value.
    /// 滚动窗口快照,QPointF 列表。X = 秒, Y = 信号值。
    Q_PROPERTY(QList<QPointF> points READ points NOTIFY pointsChanged FINAL)

    /// Left edge of the visible time window (seconds).
    /// 可见时间窗口左边界 (秒)。
    Q_PROPERTY(qreal xMin READ xMin NOTIFY xRangeChanged FINAL)

    /// Right edge of the visible time window (seconds).
    /// 可见时间窗口右边界 (秒)。
    Q_PROPERTY(qreal xMax READ xMax NOTIFY xRangeChanged FINAL)

    /// Whether the timer is currently ticking.
    /// 定时器是否正在运行。
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged FINAL)

public:
    /// Construct and assemble RingBuffer, MockSource, and QTimer.
    /// 构造并装配 RingBuffer、MockSource 与 QTimer。
    /// The timer is NOT started — QML must call start() explicitly.
    /// timer 不会启动——QML 必须显式调用 start()。
    explicit CurveController(QObject* parent = nullptr);

    /// Destructor defined in .cpp where complete core types are visible.
    /// 析构函数在 .cpp 中定义 (core 类型完整可见处)。
    ~CurveController() override;

    // --- Property accessors 属性访问器 ---

    QList<QPointF> points() const;
    qreal xMin()            const;
    qreal xMax()            const;
    bool  isRunning()       const;

public slots:
    /// Start the data pump timer.  Idempotent — no-op if already running.
    /// 启动数据泵定时器。幂等——已运行时为空操作。
    void start();

    /// Stop the data pump timer.  Idempotent — no-op if already stopped.
    /// 停止数据泵定时器。幂等——已停止时为空操作。
    void stop();

signals:
    /// Emitted on every tick after points are updated.
    /// 每次 tick 更新数据点后发射。
    void pointsChanged();

    /// Emitted when the X-axis time range changes (every tick while running).
    /// X 轴时间范围变化时发射 (运行时每次 tick)。
    void xRangeChanged();

    /// Emitted when the running state toggles.
    /// 运行状态切换时发射。
    void runningChanged();

private slots:
    /// Timer callback — executes the 7-step data flow.
    /// 定时器回调——执行七步数据流。
    void onTick();

private:
    // --- Constants 常量 ---
    // Window: 1000 points at 5000 Hz = 0.2 s visible (2 full 10 Hz periods).
    // 窗口: 5000 Hz 下 1000 点 = 0.2 秒可见范围 (10 Hz 的 2 个完整周期)。
    static constexpr int    kWindowSize       = 1000;
    static constexpr int    kProducePerTick   = 80;    // 5000 Hz × 16 ms
    static constexpr int    kRingBufCapacity  = 4096;  // 2^12, generous headroom
    static constexpr int    kTimerIntervalMs  = 16;    // ~60 fps
    static constexpr double kSampleRateHz     = 5000.0;
    static constexpr double kSignalFreqHz     = 10.0;

    // --- Core data pipeline 数据管线 ---

    /// Ring buffer: sink for MockSource, source for window pop.
    /// 环形缓冲: MockSource 的下沉,滚动窗口的拉取源。
    std::unique_ptr<indusscope::core::RingBuffer<indusscope::core::SamplePoint>> m_ringBuf;

    /// Mock signal source — deterministic, no sleep (produce() only).
    /// 模拟信号源——确定性,不睡觉 (仅用 produce())。
    std::unique_ptr<indusscope::core::MockSource> m_source;

    // --- Scrolling window 滚动窗口 ---

    /// Fixed-capacity deque window; oldest points evicted from front.
    /// 固定容量双端队列窗口;老点从前端挤出。
    std::deque<QPointF> m_window;

    /// QML-visible snapshot, refreshed each tick.
    /// QML 可见快照,每次 tick 刷新。
    QList<QPointF> m_points;

    /// X-axis range, updated each tick from window edges.
    /// X 轴范围,每次 tick 从窗口边界更新。
    qreal m_xMin = 0.0;
    qreal m_xMax = 0.2;  // expected full-window width at 5000 Hz / 5000 Hz 下预期的全窗口宽度

    // --- Timer 定时器 ---

    /// Owned by Qt parent-child (this), no manual delete needed.
    /// 由 Qt 父子关系持有 (this),无需手动 delete。
    QTimer* m_timer = nullptr;

    bool m_running = false;
};

} // namespace indusscope::ui
