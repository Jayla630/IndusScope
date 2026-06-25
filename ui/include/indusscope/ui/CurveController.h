#pragma once

#include <QObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <QThread>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>
#include <chrono>
#include <cstdint>

// Forward-declare core types to keep header free of core implementation details.
// Core types are fully included only in the .cpp translation unit.
// 前置声明 core 类型,头文件不引入 core 实现细节。core 类型仅在 .cpp 中完整包含。
namespace indusscope::core {
struct SamplePoint;
template <typename T> class RingBuffer;
} // namespace indusscope::core

class QTimer;

namespace indusscope::ui {

class AcquisitionWorker;  // forward decl / 前置声明

/// Renderer-agnostic scrolling curve adapter — pure consumer, driven by QTimer.
/// 渲染器无关的滚动曲线适配器——纯消费者,由 QTimer 驱动。
/// Production is delegated to AcquisitionWorker (owns MockSource + its own QTimer).
/// 生产委托给 AcquisitionWorker (持有 MockSource + 自己的 QTimer)。
///
/// Each timer tick (S2.3b — Min/Max downsampling pipeline):
/// 每次 timer 滴答 (S2.3b — Min/Max 降采样管线):
///   1. RingBuffer.pop_batch() → raw samples from worker thread
///   2. Write each sample into fixed-capacity circular history (overwrite oldest)
///   3. Copy valid history window to contiguous scratch buffer (handling wrap)
///   4. minmax_downsample(scratch, historyCount, kBucketCount, m_dsOut)
///   5. Map m_dsOut → QList<QPointF> m_points (timestamp_ns×1e-9 = x, value = y)
///   6. Update xMin/xMax from history edges, emit signals
///   1. RingBuffer.pop_batch() → 从 worker 线程拉取原始样本
///   2. 逐个写入固定容量环形历史 (覆盖最旧)
///   3. 将有效历史区间拷入连续 scratch 缓冲 (处理 wrap)
///   4. minmax_downsample(scratch, historyCount, kBucketCount, m_dsOut)
///   5. m_dsOut 映射为 QList<QPointF> m_points (timestamp_ns×1e-9 = x, value = y)
///   6. 从历史首尾更新 xMin/xMax,发射信号
///
/// Does NOT #include or link any chart library — the only dependencies are
/// Qt6::Core (QObject/QTimer/signals) + Qt6::Qml (QML_ELEMENT) + Qt::Gui (QPointF)
/// + indusscope::core (RingBuffer/SamplePoint).
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

    /// Last error message from AcquisitionWorker; empty string = no error.
    /// 来自 AcquisitionWorker 的最新错误消息;空字符串 = 无错误。
    /// Set by the worker thread via queued connection; safe to read from QML (UI thread).
    /// 由 worker 线程经 queued connection 设置;从 QML(UI 线程)读取安全。
    Q_PROPERTY(QString deviceError READ deviceError NOTIFY deviceErrorChanged FINAL)

public:
    /// Construct and assemble RingBuffer, AcquisitionWorker, and QTimer.
    /// 构造并装配 RingBuffer、AcquisitionWorker 与 QTimer。
    /// The timer is NOT started — QML must call start() explicitly.
    /// timer 不会启动——QML 必须显式调用 start()。
    explicit CurveController(QObject* parent = nullptr);

    /// Destructor defined in .cpp where complete core types are visible.
    /// 析构函数在 .cpp 中定义 (core 类型完整可见处)。
    ~CurveController() override;

    // --- Property accessors 属性访问器 ---

    QList<QPointF> points()      const;
    qreal xMin()                 const;
    qreal xMax()                 const;
    bool  isRunning()            const;
    QString deviceError()        const;

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

    /// Emitted when deviceError changes (set or cleared).
    /// deviceError 变化时发射(设置或清除)。
    void deviceErrorChanged();

    /// Control-plane signal — queued to worker thread to start production.
    /// 控制面信号——排队到 worker 线程启动生产。
    void startRequested();

    /// Control-plane signal — queued to worker thread to stop production.
    /// 控制面信号——排队到 worker 线程停止生产。
    void stopRequested();

private slots:
    /// Timer callback — executes the 7-step data flow.
    /// 定时器回调——执行七步数据流。
    void onTick();

    /// Receives error messages from AcquisitionWorker via queued connection;
    /// deduplicates and stores in m_deviceError, emits deviceErrorChanged().
    /// 经 queued connection 接收 AcquisitionWorker 错误消息;
    /// 去重后存入 m_deviceError,发射 deviceErrorChanged()。
    void onWorkerError(const QString& message);

private:
    // --- Constants 常量 ---
    // kHistoryCapacity: 2^17 = 131072, >= 100k with ~30% headroom.
    // kHistoryCapacity: 2^17 = 131072, ≥100k 留约 30% 头寸。
    static constexpr int    kHistoryCapacity  = 131072; // 2^17, power of 2 for & mask / 2 的幂,用于 & 掩码
    static constexpr int    kHistoryMask      = kHistoryCapacity - 1;
    // kBucketCount: ~screen width in pixels; produces ≤2×bucket_count render points.
    // kBucketCount: 屏宽量级像素数;产生 ≤2×桶数个渲染点。
    static constexpr int    kBucketCount      = 1000;
    static constexpr int    kProducePerTick   = 80;    // 5000 Hz × 16 ms / 5000 Hz × 16 ms
    static constexpr int    kRingBufCapacity  = 4096;  // 2^12, generous headroom / 充裕头寸
    static constexpr int    kTimerIntervalMs  = 16;    // ~60 fps
    static constexpr int    kLatencySamples   = 3000;  // ~50 s @ 60 fps; enough for stable p99 / ~50s@60fps,足够稳定 p99

    // --- Core data pipeline 数据管线 ---

    /// Ring buffer: sink for ProtocolSource, source for window pop.
    /// 环形缓冲: MockSource 的下沉,滚动窗口的拉取源。
    /// Declared before m_thread so it is destroyed last (reverse decl order);
    /// combined with ~CurveController() quit→wait this is defense-in-depth against UAF.
    /// 声明在 m_thread 之前,因此最后析构 (逆序析构);配合析构函数 quit→wait 纵深防御 UAF。
    std::unique_ptr<indusscope::core::RingBuffer<indusscope::core::SamplePoint>> m_ringBuf;

    /// Worker thread — started once at construction, quit only in destructor.
    /// 工作线程——构造时启动一次,仅在析构中 quit。
    /// Event loop idles until startRequested() is emitted; reusable across
    /// start/stop cycles without thread churn.
    /// 事件循环空转直到发射 startRequested();可在 start/stop 周期重复使用,不反复创建线程。
    /// Declared after m_ringBuf → destroyed before it (reverse decl order).
    /// 声明在 m_ringBuf 之后 → 先于 m_ringBuf 析构 (逆序)。
    QThread m_thread;

    // --- History ring — S2.3b fixed-capacity circular buffer / 历史环——S2.3b 固定容量环形缓冲 ---

    /// Fixed-capacity circular history, stores raw SamplePoint (not QPointF).
    /// 固定容量环形历史,存储原始 SamplePoint (非 QPointF)。
    /// Pre-allocated once at construction; overwrites oldest on wrap.
    /// 构造时一次预分配;回绕时覆盖最旧。
    std::vector<indusscope::core::SamplePoint> m_history;

    /// Next write position in m_history (0 .. kHistoryCapacity-1).
    /// m_history 中下一写入位置 (0 .. kHistoryCapacity-1)。
    std::size_t m_historyWriteIdx = 0;

    /// Number of valid points currently in history (0 .. kHistoryCapacity).
    /// 当前历史中有效点数 (0 .. kHistoryCapacity)。
    std::size_t m_historyCount = 0;

    /// Contiguous scratch buffer for feeding minmax_downsample (pre-allocated).
    /// 连续 scratch 缓冲,供 minmax_downsample 喂入 (预分配)。
    std::vector<indusscope::core::SamplePoint> m_scratch;

    /// Downsample output vector — reused across ticks, resize by minmax_downsample.
    /// 降采样输出 vector——跨 tick 复用,由 minmax_downsample resize。
    std::vector<indusscope::core::SamplePoint> m_dsOut;

    /// QML-visible snapshot, refreshed each tick from downsample output.
    /// QML 可见快照,每次 tick 从降采样输出刷新。
    QList<QPointF> m_points;

    /// X-axis range, updated each tick from history timestamp edges.
    /// X 轴范围,每次 tick 从历史时间戳边界更新。
    qreal m_xMin = 0.0;
    qreal m_xMax = 0.2;  // expected full-window width at 5000 Hz / 5000 Hz 下预期的全窗口宽度

    // --- FPS counter 帧率计数 ---

    /// Incremented by frameSwapped signal on GUI thread (queued connection).
    /// 由 GUI 线程上的 frameSwapped 信号递增 (排队连接)。
    int m_frameCount = 0;

    // --- Latency measurement 延迟测量 ---

    /// Wall-clock timestamp captured in onTick() after m_points is assembled, before emit.
    /// onTick() 中 m_points 拼装完成后、emit 之前的墙钟时间戳。
    std::chrono::steady_clock::time_point m_dataReadyNs{};

    /// True when a new frame's data-ready timestamp is pending consumption by frameSwapped.
    /// 新帧数据就绪时间戳等待 frameSwapped 消费时为真。
    bool m_pendingFrame = false;

    /// Ring buffer of per-frame render latencies in nanoseconds, pre-allocated.
    /// 每帧渲染延迟纳秒值的环形收集缓冲,预分配。
    std::vector<int64_t> m_latencies;

    /// Set to true once kLatencySamples have been collected; stops further collection.
    /// 收集满 kLatencySamples 后置真;停止继续收集。
    bool m_latenciesCollected = false;

    /// Timestamp captured in start() for warmup gating (skip first ~1 s of frames).
    /// start() 中捕获的时间戳,用于预热门控 (跳过前约 1 秒帧)。
    std::chrono::steady_clock::time_point m_startTimeNs{};

    // --- Timer 定时器 ---

    /// Owned by Qt parent-child (this), no manual delete needed.
    /// 由 Qt 父子关系持有 (this),无需手动 delete。
    QTimer* m_timer = nullptr;

    /// Producer worker — owns ProtocolSource + its own QTimer; moved to m_thread via moveToThread.
    /// 生产者 worker——持有 MockSource + 自己的 QTimer;通过 moveToThread 移到 m_thread。
    /// Lifetime managed by connect(&m_thread, finished, m_worker, deleteLater) — no parent.
    /// 生命周期由 connect(&m_thread, finished, m_worker, deleteLater) 管理——无 parent。
    AcquisitionWorker* m_worker = nullptr;

    bool m_running = false;

    /// Last error from AcquisitionWorker; empty = no error / device online.
    /// 来自 AcquisitionWorker 的最新错误;空 = 无错误/设备在线。
    QString m_deviceError;
};

} // namespace indusscope::ui
