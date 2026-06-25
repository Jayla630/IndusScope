import QtQuick
import QtGraphs
import IndusScope.Ui 1.0  // Qt 6: must explicitly import own module for C++ types / Qt 6: 需显式导入自身模块以访问 C++ 类型

/// S1.4c — scrolling real-time curve, oscilloscope-style.
/// S1.4c — 示波器式实时滚动曲线。
///
/// Data flow: QTimer (16 ms) → MockSource.produce(80) → RingBuffer
/// → pop_batch → fixed 1000-point deque window → LineSeries.replace.
/// 数据流: QTimer (16 ms) → MockSource.produce(80) → RingBuffer
/// → pop_batch → 固定 1000 点 deque 窗口 → LineSeries.replace。
Window {
    visible: true
    title: "IndusScope"
    width: 800
    height: 600

    // CurveController drives the data pump; QML calls start() on completed.
    // CurveController 驱动数据泵;QML 在完成时调用 start()。
    CurveController {
        id: controller
    }

    GraphsView {
        anchors.fill: parent
        anchors.margins: 16

        // X axis: time in seconds, dynamically tracks the scrolling window.
        // X 轴: 秒,动态跟踪滚动窗口范围。
        axisX: ValueAxis {
            min: controller.xMin
            max: controller.xMax
            labelDecimals: 3
            titleText: "Time (s)"
        }

        // Y axis: fixed range ±1.2 for 10 Hz sine amplitude ±1.0 with margin.
        // Y 轴: 固定范围 ±1.2,10 Hz 正弦 ±1.0 加边距。
        axisY: ValueAxis {
            min: -1.2
            max: 1.2
            labelDecimals: 1
            titleText: "Amplitude"
        }

        LineSeries {
            id: lineSeries
        }
    }

    // Replace all data points every time the controller pushes a new window.
    // 每次控制器推送新窗口时替换全部数据点。
    Connections {
        target: controller
        function onPointsChanged() { lineSeries.replace(controller.points) }
    }

    // Device offline banner — visible only when worker reports an error.
    // 设备离线横幅——仅在 worker 报告错误时可见。
    Rectangle {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 8
        width: offlineText.implicitWidth + 24
        height: offlineText.implicitHeight + 12
        color: "#cc2200"
        radius: 4
        visible: controller.deviceError.length > 0

        Text {
            id: offlineText
            anchors.centerIn: parent
            text: "device offline"
            color: "white"
            font.pixelSize: 14
            font.bold: true
        }
    }

    // Explicitly start the data pump — no hidden side effects in the constructor.
    // 显式启动数据泵——构造函数无隐藏副作用。
    Component.onCompleted: {
        controller.start()
    }
}
