import QtQuick
import IndusScope.Ui 1.0  // Qt 6: must explicitly import own module for C++ types / Qt 6: 需显式导入自身模块以访问 C++ 类型

/// S2.6b-2 — single-threaded first light: synthetic frames on screen.
/// S2.6b-2 — 单线程首光:合成帧上屏。
///
/// A standalone window kept fully separate from the curve UI (Main.qml). Selected
/// at launch via env INDUSSCOPE_VIEW=frame. A white bar sweeps left→right over a
/// color gradient at ~30 fps. / 与曲线 UI(Main.qml)完全隔离的独立窗口,启动时由
/// env INDUSSCOPE_VIEW=frame 选择。白竖条在彩色渐变上以约 30fps 从左扫到右。
Window {
    visible: true
    title: "IndusScope — FrameView"
    width: 1280
    height: 720

    // FrameView is the custom QQuickItem; it owns its frame pump and source.
    // FrameView 是自定义 QQuickItem;自持帧泵与帧源。
    FrameView {
        id: frame
        anchors.fill: parent
        frameWidth: 1280
        frameHeight: 720
    }

    // Explicitly start the frame pump — no hidden side effects in the constructor.
    // 显式启动帧泵——构造函数无隐藏副作用。
    Component.onCompleted: frame.start()
}
