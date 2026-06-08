#include "indusscope/ui/CurveController.h"
#include "indusscope/ui/AcquisitionWorker.h"

#include "indusscope/core/MinMaxDownsampler.h"
#include "indusscope/core/RingBuffer.h"
#include "indusscope/core/SamplePoint.h"
#include "indusscope/protocol/ProtocolConfig.h"
#include "indusscope/protocol/ProtocolFactory.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QTimer>
#include <QThread>
#include <QDebug>

#include <algorithm>   // std::sort

namespace indusscope::ui {

CurveController::CurveController(QObject* parent)
    : QObject(parent)
    , m_ringBuf(std::make_unique<indusscope::core::RingBuffer<indusscope::core::SamplePoint>>(
          kRingBufCapacity))
    , m_history(kHistoryCapacity)      // pre-allocate; filled by index, no push_back / 预分配;按索引填入,无 push_back
    , m_scratch(kHistoryCapacity)      // pre-allocate; worst-case full history copy / 预分配;最坏情况全量历史拷贝
    , m_timer(new QTimer(this))        // parent=this → Qt parent-child ownership, RAII
                                       // parent=this → Qt 父子所有权,RAII
{
    m_latencies.reserve(kLatencySamples);

    qDebug() << "[UI] CurveController ctor on thread" << QThread::currentThreadId();

    // --- Assemble AcquisitionWorker (producer half) 装配 AcquisitionWorker (生产者半场) ---

    // create("mock") returns nullptr only if MockProtocol.cpp TU was not linked
    // (requires WHOLE_ARCHIVE in app/CMakeLists.txt — already set).
    // create("mock") 仅在 MockProtocol.cpp TU 未被链接时返回 nullptr
    // (需要 app/CMakeLists.txt 中的 WHOLE_ARCHIVE——已设置)。
    auto proto = indusscope::protocol::ProtocolFactory::instance().create("mock");
    Q_ASSERT_X(proto != nullptr, "CurveController", "ProtocolFactory::create(\"mock\") returned nullptr — "
               "check WHOLE_ARCHIVE linkage of IndusScope::protocol");

    indusscope::protocol::ProtocolConfig cfg;
    cfg.params["waveform"]     = "sine";
    cfg.params["channels"]     = "1";
    cfg.params["period_ns"]    = "200000";  // 200 µs = 5000 Hz, matches kProducePerTick×kTimerIntervalMs
                                            // 200 µs = 5000 Hz,对齐 kProducePerTick×kTimerIntervalMs
    cfg.params["amplitude"]    = "1.0";
    cfg.params["frequency_hz"] = "10.0";   // 10 Hz → 2 full periods in 0.2 s window
                                            // 10 Hz → 0.2 s 窗口内 2 个完整周期
    cfg.params["phase_rad"]    = "0.0";

    // No parent — moveToThread forbids parent QObject; lifecycle via deleteLater below.
    // 无 parent——moveToThread 禁止父 QObject;生命周期由下方的 deleteLater 管理。
    m_worker = new AcquisitionWorker(std::move(proto), *m_ringBuf, cfg, /*channel=*/0);

    // Move worker (and its child QTimer) to dedicated thread.
    // 将 worker (及其子 QTimer) 移到专用线程。
    m_worker->moveToThread(&m_thread);

    // Lifecycle: delete worker when thread event loop ends.
    // 生命周期: 线程事件循环结束时删除 worker。
    // This is the canonical moveToThread ownership pattern — not a bare-new leak.
    // 这是 moveToThread 公认的所有权模式——非裸 new 泄漏。
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    // --- Control plane: queued signals from UI thread → worker thread 控制面: UI 线程 → worker 线程的排队信号 ---

    // Explicit Qt::QueuedConnection so reviewers can verify at a glance.
    // 显式 Qt::QueuedConnection, review 时一眼可验。
    connect(this, &CurveController::startRequested,
            m_worker, &AcquisitionWorker::start,
            Qt::QueuedConnection);
    connect(this, &CurveController::stopRequested,
            m_worker, &AcquisitionWorker::stop,
            Qt::QueuedConnection);

    // --- Configure render timer (not started — QML calls start()) 配置渲染定时器 (不启动——QML 调用 start()) ---

    m_timer->setInterval(kTimerIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &CurveController::onTick);

    // --- Start worker thread 启动 worker 线程 ---
    // Event loop idles until startRequested() is emitted.
    // 事件循环空转直到发射 startRequested()。
    m_thread.start();

    // --- FPS counter: wire QQuickWindow::frameSwapped via queued connection 帧率计数:通过排队连接接入 QQuickWindow::frameSwapped ---
    // Delay to event loop so the QQuickWindow is known to exist by then.
    // 延迟到事件循环,此时 QQuickWindow 已知存在。
    // AutoConnection demotes to QueuedConnection because receiver (this) is on GUI thread
    // while frameSwapped fires from the render thread — no race on m_frameCount.
    // AutoConnection 因 receiver (this) 在 GUI 线程、frameSwapped 从渲染线程发射,
    // 自动降为 QueuedConnection——m_frameCount 无竞争。
    QMetaObject::invokeMethod(this, [this] {
        const auto wins = QGuiApplication::topLevelWindows();
        for (QWindow* w : wins) {
            auto* qw = qobject_cast<QQuickWindow*>(w);
            if (qw) {
                connect(qw, &QQuickWindow::frameSwapped, this,
                        [this] {
                            if (!m_running) return;
                            ++m_frameCount;
                            if (m_pendingFrame && !m_latenciesCollected) {
                                const auto now = std::chrono::steady_clock::now();
                                if (now - m_startTimeNs > std::chrono::seconds(1)) {
                                    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        now - m_dataReadyNs).count();
                                    m_latencies.push_back(ns);
                                    if (m_latencies.size() >= static_cast<std::size_t>(kLatencySamples)) {
                                        std::sort(m_latencies.begin(), m_latencies.end());
                                        const std::size_t N = m_latencies.size();
                                        const double p50 = static_cast<double>(m_latencies[N * 50 / 100]) / 1'000'000.0;
                                        const double p99 = static_cast<double>(m_latencies[N * 99 / 100]) / 1'000'000.0;
                                        const double max = static_cast<double>(m_latencies.back()) / 1'000'000.0;
                                        qInfo() << "[Latency]" << "p50:" << p50 << "ms"
                                                << "p99:" << p99 << "ms"
                                                << "max:" << max << "ms"
                                                << "(n=" << N << ")";
                                        m_latenciesCollected = true;
                                    }
                                }
                                m_pendingFrame = false;
                            }
                        });
                qDebug() << "[UI] FPS counter hooked to QQuickWindow";
                return;
            }
        }
        qWarning() << "[UI] FPS counter: no QQuickWindow found — frameSwapped not connected";
    }, Qt::QueuedConnection);

    // --- 1-second FPS reporter 每秒 FPS 报告 ---
    QTimer* fpsTimer = new QTimer(this);
    fpsTimer->setInterval(1000);
    connect(fpsTimer, &QTimer::timeout, this, [this] {
        if (!m_running)
            return;
        const auto wins = QGuiApplication::topLevelWindows();
        bool hasQQuick = false;
        for (QWindow* w : wins) {
            if (qobject_cast<QQuickWindow*>(w)) {
                hasQQuick = true;
                break;
            }
        }
        if (!hasQQuick) {
            qWarning() << "[UI] FPS counter: no QQuickWindow found — frameSwapped not connected";
            return;
        }
        qInfo() << "[FPS]" << m_frameCount;
        m_frameCount = 0;
    });
    fpsTimer->setTimerType(Qt::VeryCoarseTimer); // 1 s precision, low overhead / 1 秒精度,低开销
    fpsTimer->start();
}

CurveController::~CurveController()
{
    // Shutdown dance — prevent UAF on worker / ring:
    // 关机舞步——防止 worker / ring 的 use-after-free:
    // 1. Stop consumer timer so no more pop_batch() from UI thread.
    //    停止消费定时器,UI 线程不再 pop_batch()。
    // 2. Quit worker event loop → deleteLater fires → worker destroyed on worker thread.
    //    退出 worker 事件循环 → deleteLater 触发 → worker 在 worker 线程析构。
    // 3. Wait for worker thread to fully finish before ~QThread runs.
    //    等待 worker 线程完全结束,再让 ~QThread 执行。
    // After this body, members destruct in reverse decl order:
    // m_thread (destroyed first, but already idle) → ... → m_ringBuf (destroyed last).
    // 本函数体返回后,成员按声明逆序析构: m_thread (最先析构,但已空闲) → ... → m_ringBuf (最后析构)。
    m_timer->stop();
    m_thread.quit();
    m_thread.wait();
}

// --- Property accessors 属性访问器 ---

QList<QPointF> CurveController::points() const { return m_points; }
qreal CurveController::xMin()            const { return m_xMin; }
qreal CurveController::xMax()            const { return m_xMax; }
bool  CurveController::isRunning()       const { return m_running; }

// --- Public slots 公开槽 ---

void CurveController::start()
{
    if (m_running)
        return;
    m_running = true;
    m_startTimeNs = std::chrono::steady_clock::now();
    qDebug() << "[UI] start() on thread" << QThread::currentThreadId();
    emit startRequested();   // queued to worker thread — production begins asynchronously
                             // 排队到 worker 线程——异步开始生产
    m_timer->start();
    emit runningChanged();
}

void CurveController::stop()
{
    if (!m_running)
        return;
    emit stopRequested();    // queued to worker thread — production stops asynchronously
                             // 排队到 worker 线程——异步停止生产
    m_timer->stop();
    m_pendingFrame = false;
    m_running = false;
    emit runningChanged();
}

// --- Private slots 私有槽 ---

void CurveController::onTick()
{
    // Step 1: Pop all available samples from RingBuffer (producer is now AcquisitionWorker).
    // 步骤 1: 从环形缓冲捞出所有可用样本 (生产者现在是 AcquisitionWorker)。
    indusscope::core::SamplePoint buf[kProducePerTick];  // stack buffer, max expected per tick
                                                         // 栈缓冲,每次 tick 最大预期量
    const std::size_t n = m_ringBuf->pop_batch(buf, kProducePerTick);
    if (n == 0)
        return;

    // Step 2: Write each new sample into the fixed-capacity circular history ring.
    // 步骤 2: 将每个新样本写入固定容量环形历史。
    // Overwrite oldest when full; m_historyWriteIdx wraps via & mask.
    // 满时覆盖最旧; m_historyWriteIdx 通过 & mask 回绕。
    for (std::size_t i = 0; i < n; ++i) {
        m_history[m_historyWriteIdx] = buf[i];
        m_historyWriteIdx = (m_historyWriteIdx + 1) & kHistoryMask;
    }
    // Update count — saturate at capacity (overwrites have already happened above).
    // 更新计数——饱和在容量上限 (覆盖已在上方发生)。
    m_historyCount = (m_historyCount + n > static_cast<std::size_t>(kHistoryCapacity))
                         ? static_cast<std::size_t>(kHistoryCapacity)
                         : m_historyCount + n;

    // Step 3: Copy valid history window into contiguous scratch buffer (handling wrap).
    // 步骤 3: 将有效历史区间拷入连续 scratch 缓冲 (处理 wrap)。
    if (m_historyCount == 0)
        return;

    if (m_historyCount < static_cast<std::size_t>(kHistoryCapacity)) {
        // Not yet wrapped — single segment [0, m_historyCount).
        // 尚未回绕——单段 [0, m_historyCount)。
        std::copy(m_history.begin(), m_history.begin() + static_cast<std::ptrdiff_t>(m_historyCount),
                  m_scratch.begin());
    } else {
        // Wrapped — two segments: [writeIdx, cap) + [0, writeIdx).
        // 已回绕——两段: [writeIdx, cap) + [0, writeIdx)。
        const std::size_t seg1 = kHistoryCapacity - m_historyWriteIdx; // points from writeIdx to end
                                                                        // 从 writeIdx 到尾部的点数
        std::copy(m_history.begin() + static_cast<std::ptrdiff_t>(m_historyWriteIdx),
                  m_history.end(),
                  m_scratch.begin());
        std::copy(m_history.begin(),
                  m_history.begin() + static_cast<std::ptrdiff_t>(m_historyWriteIdx),
                  m_scratch.begin() + static_cast<std::ptrdiff_t>(seg1));
    }

    // Step 4: Min/Max downsampling on the FULL history (not just this tick's batch).
    // 步骤 4: 对全历史做 Min/Max 降采样 (不是这一 tick 的批次)。
    // This is the critical correctness point — downsampling only new points would
    // discard the signal envelope that has accumulated across the entire window.
    // 这是关键正确性点——仅降采样新点会丢弃整个窗口已累积的信号包络。
    indusscope::core::minmax_downsample(m_scratch.data(), m_historyCount,
                                        static_cast<std::size_t>(kBucketCount), m_dsOut);

    // Step 5: Map downsample output → QList<QPointF> for QML binding.
    // 步骤 5: 降采样输出映射为 QList<QPointF>,供 QML 绑定。
    constexpr double kNanosPerSec = 1'000'000'000.0;
    m_points.clear();
    m_points.reserve(static_cast<int>(m_dsOut.size()));
    for (const auto& sp : m_dsOut) {
        m_points.append(QPointF(
            static_cast<double>(sp.timestamp_ns) / kNanosPerSec,
            sp.value));
    }

    // Step 6: Update X-axis range from history edges (scratch is in source order).
    // 步骤 6: 从历史边界更新 X 轴范围 (scratch 按源序排列)。
    if (m_historyCount > 0) {
        m_xMin = static_cast<double>(m_scratch[0].timestamp_ns) / kNanosPerSec;
        m_xMax = static_cast<double>(m_scratch[m_historyCount - 1].timestamp_ns) / kNanosPerSec;
    }

    // Step 6.5: Stamp data-ready wall clock for render latency measurement.
    // 步骤 6.5: 打数据就绪墙钟戳供渲染延迟测量。
    m_dataReadyNs = std::chrono::steady_clock::now();
    m_pendingFrame = true;

    // Step 7: Emit signals so QML rebinds.
    // 步骤 7: 发射信号使 QML 重新绑定。
    emit pointsChanged();
    emit xRangeChanged();
}

} // namespace indusscope::ui
