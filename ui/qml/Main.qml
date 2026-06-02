import QtQuick
import QtGraphs
import IndusScope.Ui 1.0  // Qt 6: must explicitly import own module for C++ types / Qt 6: 需显式导入自身模块以访问 C++ 类型

/// S1.4b — static sine curve via adapter + QtGraphs.
/// S1.4b — 通过适配器 + QtGraphs 绘制静态正弦曲线。
Window {
    visible: true
    title: "IndusScope"
    width: 800
    height: 600

    // CurveController computes a 500-point 10 Hz sine window at construct time.
    // CurveController 构造时计算 500 点 10 Hz 正弦窗口。
    CurveController {
        id: controller
    }

    GraphsView {
        anchors.fill: parent
        anchors.margins: 16

        axisX: ValueAxis {
            // X axis: time in seconds, one full 10 Hz period (0–0.1 s).
            // X 轴: 秒, 10 Hz 一个完整周期 (0–0.1 秒)。
            min: 0.0
            max: 0.1
            labelDecimals: 3
            titleText: "Time (s)"
        }

        axisY: ValueAxis {
            // Y axis: sine amplitude ±1.0 with small margin.
            // Y 轴: 正弦幅值 ±1.0, 留小边距。
            min: -1.2
            max: 1.2
            labelDecimals: 1
            titleText: "Amplitude"
        }

        LineSeries {
            id: lineSeries
            // Data fed programmatically in Component.onCompleted below.
            // 数据在下方的 Component.onCompleted 中程序化注入。
        }
    }

    Component.onCompleted: {
        // One-shot feed: replace() clears any existing data and appends all.
        // 一次性注入: replace() 清除旧数据并追加全部点。
        lineSeries.replace(controller.points)
    }
}
