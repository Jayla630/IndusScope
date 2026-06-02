#pragma once

#include <QObject>
#include <QList>
#include <QPointF>
#include <QtQml/qqmlregistration.h>

namespace indusscope::ui {

/// Renderer-agnostic curve adapter — computes one window of deterministic
/// sine data at construction time and exposes it as QList<QPointF> for
/// consumption by any QML chart component (QtCharts / QtGraphs / Canvas).
/// 渲染器无关的曲线适配器——构造时算一窗确定性正弦数据,以 QList<QPointF>
/// 暴露给任意 QML 图表组件 (QtCharts / QtGraphs / Canvas) 消费。
///
/// Does NOT #include or link any chart library — the only dependency is
/// indusscope::core::SignalGenerator.  This keeps the renderer replaceable
/// without touching the adapter.
/// 不 #include 或链接任何图表库——唯一依赖是 indusscope::core::SignalGenerator。
/// 渲染器可替换而不改适配器一行。
class CurveController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    /// Pre-computed curve points; X = time in seconds, Y = signal value.
    /// 预计算曲线点;X = 秒, Y = 信号值。
    Q_PROPERTY(QList<QPointF> points READ points NOTIFY pointsChanged FINAL)

public:
    /// Construct and compute one window of sine data.
    /// 构造并计算一窗正弦数据。
    ///
    /// Default signal: 10 Hz pure sine, amplitude 1.0, 500 points
    /// spanning 100 ms (= exactly one full period at 10 Hz).
    /// 默认信号: 10 Hz 纯正弦,幅值 1.0,500 点覆盖 100 ms (= 10 Hz 恰好一个完整周期)。
    explicit CurveController(QObject* parent = nullptr);

    /// Returns the pre-computed curve points (X=seconds, Y=signal value).
    /// 返回预计算的曲线点 (X=秒, Y=信号值)。
    QList<QPointF> points() const;

signals:
    /// Emitted when points change (reserved for S1.4c scrolling).
    /// 当数据点变化时发出 (预留给 S1.4c 滚动使用)。
    void pointsChanged();

private:
    QList<QPointF> m_points;
};

} // namespace indusscope::ui
