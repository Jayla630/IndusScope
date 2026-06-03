#include "indusscope/ui/CurveController.h"
#include "indusscope/ui/AcquisitionWorker.h"

#include "indusscope/core/MockSource.h"
#include "indusscope/core/RingBuffer.h"
#include "indusscope/core/SamplePoint.h"
#include "indusscope/core/SignalGenerator.h"

#include <QTimer>
#include <QThread>
#include <QDebug>

namespace indusscope::ui {

CurveController::CurveController(QObject* parent)
    : QObject(parent)
    , m_ringBuf(std::make_unique<indusscope::core::RingBuffer<indusscope::core::SamplePoint>>(
          kRingBufCapacity))
    , m_timer(new QTimer(this))  // parent=this → Qt parent-child ownership, RAII
                                 // parent=this → Qt 父子所有权,RAII
{
    using indusscope::core::MockSourceConfig;
    using indusscope::core::SignalConfig;

    qDebug() << "[UI] CurveController ctor on thread" << QThread::currentThreadId();

    // --- Assemble AcquisitionWorker (producer half) 装配 AcquisitionWorker (生产者半场) ---

    MockSourceConfig cfg;
    cfg.sample_rate_hz      = kSampleRateHz;    // 5000 Hz
    cfg.start_timestamp_ns  = 0;
    cfg.signal_config.amplitude    = 1.0;
    cfg.signal_config.frequency_hz = kSignalFreqHz;  // 10 Hz → 2 full periods in 0.2 s window
                                                      // 10 Hz → 0.2 s 窗口内 2 个完整周期
    cfg.signal_config.noise_stddev = 0.0;            // pure sine for visual verification
                                                      // 纯正弦便于目视验证

    // No parent — moveToThread forbids parent QObject; lifecycle via deleteLater below.
    // 无 parent——moveToThread 禁止父 QObject;生命周期由下方的 deleteLater 管理。
    m_worker = new AcquisitionWorker(*m_ringBuf, cfg);

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

    // Start worker thread — event loop idles until startRequested() is emitted.
    // 启动 worker 线程——事件循环空转直到发射 startRequested()。
    m_thread.start();
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

    // Step 3: Push new samples into the scrolling window.
    // 步骤 3: 新样本推入滚动窗口。
    constexpr double kNanosPerSec = 1'000'000'000.0;
    for (std::size_t i = 0; i < n; ++i) {
        // X = timestamp in seconds (start_timestamp_ns=0, so direct conversion).
        // X = 纳秒时间戳转秒 (start_timestamp_ns=0,直接换算)。
        double t_sec = static_cast<double>(buf[i].timestamp_ns) / kNanosPerSec;
        m_window.push_back(QPointF(t_sec, buf[i].value));
    }

    // Step 4: Trim to fixed capacity — oldest points evicted from front.
    // 步骤 4: 裁剪至固定容量——老点从前端挤出。
    while (m_window.size() > static_cast<std::size_t>(kWindowSize))
        m_window.pop_front();

    // Step 5: Snapshot for QML property.
    // 步骤 5: 生成 QML 属性快照。
    m_points = QList<QPointF>(m_window.begin(), m_window.end());

    // Step 6: Update X-axis range from window edges.
    // 步骤 6: 从窗口边界更新 X 轴范围。
    if (!m_window.empty()) {
        m_xMin = m_window.front().x();
        m_xMax = m_window.back().x();
    }

    // Step 7: Emit signals so QML rebinds.
    // 步骤 7: 发射信号使 QML 重新绑定。
    emit pointsChanged();
    emit xRangeChanged();
}

} // namespace indusscope::ui
