#include "indusscope/ui/CurveController.h"

#include "indusscope/core/SignalGenerator.h"

namespace indusscope::ui {

CurveController::CurveController(QObject* parent)
    : QObject(parent)
{
    using indusscope::core::SignalConfig;
    using indusscope::core::SignalGenerator;

    // 10 Hz pure sine, no noise — deterministic for visual verification.
    // 10 Hz 纯正弦,无噪声——确定性输出便于目视验证。
    SignalConfig cfg;
    cfg.amplitude = 1.0;
    cfg.frequency_hz = 10.0;
    cfg.noise_stddev = 0.0;

    SignalGenerator gen(cfg);

    // 500 points × 0.2 ms = 100 ms = exactly 1 period of 10 Hz sine.
    // 500 点 × 0.2 ms = 100 ms = 10 Hz 正弦恰好一个完整周期。
    constexpr int kPointCount = 500;
    constexpr double kDtSec = 0.0002;          // 0.2 ms per sample / 每采样 0.2 ms
    constexpr std::int64_t kDtNs = 200'000;    // 200 000 ns per sample / 每采样 200 000 ns

    m_points.reserve(kPointCount);
    for (int i = 0; i < kPointCount; ++i) {
        double t_sec = static_cast<double>(i) * kDtSec;
        std::int64_t t_ns = static_cast<std::int64_t>(i) * kDtNs;
        double y = gen.value(t_ns);
        m_points.append(QPointF(t_sec, y));
    }
}

QList<QPointF> CurveController::points() const
{
    return m_points;
}

} // namespace indusscope::ui
