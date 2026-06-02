#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>

// Static QML plugin import — required when indusscope_ui is linked statically.
// The plugin class name is URI "IndusScope.Ui" with dots → underscores + "Plugin".
// 静态 QML 插件导入 — indusscope_ui 以静态库链接时必须显式导入。
// 插件类名由 URI "IndusScope.Ui" 将点号替换为下划线并追加 "Plugin" 生成。
Q_IMPORT_QML_PLUGIN(IndusScope_UiPlugin)

/// S1.4c — Qt Quick entry point with scrolling CurveController adapter.
/// S1.4c — Qt Quick 入口,含滚动 CurveController 适配器。
int main(int argc, char *argv[])
{
    // QGuiApplication is sufficient for Qt Quick scene-graph rendering;
    // QApplication (QtWidgets) is not needed and not linked.
    // QGuiApplication 足够 Qt Quick 场景图渲染;无需 QApplication (QtWidgets),未链接。
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    // Graceful exit if QML module fails to load (e.g. missing plugin).
    // QML 模块加载失败时优雅退出 (如插件缺失)。
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);

    // Module URI: "IndusScope.Ui" matches qt_add_qml_module URI in ui/CMakeLists.txt.
    // Requires QTP0001 NEW so the module is at :/qt/qml/ (default QML import path).
    // 模块 URI: "IndusScope.Ui" 对齐 ui/CMakeLists.txt 中 qt_add_qml_module 的 URI。
    // 需要 QTP0001 NEW,将模块放在 :/qt/qml/ (默认 QML 导入路径)。
    engine.loadFromModule("IndusScope.Ui", "Main");

    return app.exec();
}
