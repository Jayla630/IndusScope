#include "indusscope/ui/CurveController.h"

#include "indusscope/core/MockSource.h"
#include "indusscope/core/RingBuffer.h"
#include "indusscope/core/SamplePoint.h"
#include "indusscope/core/SignalGenerator.h"

#include <QTimer>

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

    // --- Assemble MockSource 装配 MockSource ---

    MockSourceConfig cfg;
    cfg.sample_rate_hz      = kSampleRateHz;    // 5000 Hz
    cfg.start_timestamp_ns  = 0;
    cfg.signal_config.amplitude    = 1.0;
    cfg.signal_config.frequency_hz = kSignalFreqHz;  // 10 Hz → 2 full periods in 0.2 s window
                                                      // 10 Hz → 0.2 s 窗口内 2 个完整周期
    cfg.signal_config.noise_stddev = 0.0;            // pure sine for visual verification
                                                      // 纯正弦便于目视验证

    m_source = std::make_unique<indusscope::core::MockSource>(cfg, *m_ringBuf);

    // --- Configure timer (not started — QML calls start()) 配置定时器 (不启动——QML 调用 start()) ---

    m_timer->setInterval(kTimerIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &CurveController::onTick);
}

CurveController::~CurveController() = default;
// Defined here where RingBuffer<SamplePoint> and MockSource are complete types.
// 在此定义,此时 RingBuffer<SamplePoint> 与 MockSource 为完整类型。

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
    m_timer->start();
    emit runningChanged();
}

void CurveController::stop()
{
    if (!m_running)
        return;
    m_timer->stop();
    m_running = false;
    emit runningChanged();
}

// --- Private slots 私有槽 ---

void CurveController::onTick()
{
    // Step 1: Produce k samples into RingBuffer.
    // 步骤 1: 产出 k 个样本到环形缓冲。
    m_source->produce(kProducePerTick);

    // Step 2: Pop all available samples from RingBuffer.
    // 步骤 2: 从环形缓冲捞出所有可用样本。
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
