#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>

#include "indusscope/protocol/PluginLoader.h"

// Static QML plugin import — required when indusscope_ui is linked statically.
// The plugin class name is URI "IndusScope.Ui" with dots → underscores + "Plugin".
// 静态 QML 插件导入 — indusscope_ui 以静态库链接时必须显式导入。
// 插件类名由 URI "IndusScope.Ui" 将点号替换为下划线并追加 "Plugin" 生成。
Q_IMPORT_QML_PLUGIN(IndusScope_UiPlugin)

/// S2.5d-2 — Qt Quick entry point with protocol-mode selection via env var.
/// S2.5d-2 — Qt Quick 入口,通过环境变量选择协议模式。
int main(int argc, char *argv[])
{
    // QGuiApplication is sufficient for Qt Quick scene-graph rendering;
    // QApplication (QtWidgets) is not needed and not linked.
    // QGuiApplication 足够 Qt Quick 场景图渲染;无需 QApplication (QtWidgets),未链接。
    QGuiApplication app(argc, argv);

    // --- Protocol plugin loading — must happen before engine.loadFromModule() ---
    // --- 协议插件加载——必须在 engine.loadFromModule() 之前 ---
    // PluginLoader lives until main() returns — keeps the DL handle alive.
    // PluginLoader 存活至 main() 返回——保持 DL handle 有效。
    indusscope::protocol::PluginLoader pluginLoader;

    const QString mode = qEnvironmentVariable("INDUSSCOPE_PROTOCOL", "mock");
    if (mode == QStringLiteral("modbus")) {
        // Plugin path: same directory as the executable (CWD-independent).
        // 插件路径: 与可执行文件同目录 (与 CWD 无关)。
        // env INDUSSCOPE_MODBUS_PLUGIN overrides the default path if set.
        // 若设置 env INDUSSCOPE_MODBUS_PLUGIN,则覆盖默认路径。
        const QString defaultPlugin = QCoreApplication::applicationDirPath()
#if defined(_WIN32)
            + QStringLiteral("/libprotocol_modbus.dll");
#elif defined(__APPLE__)
            + QStringLiteral("/libprotocol_modbus.dylib");
#else
            + QStringLiteral("/libprotocol_modbus.so");
#endif
        const QString pluginPath = qEnvironmentVariable("INDUSSCOPE_MODBUS_PLUGIN", defaultPlugin);

        if (!pluginLoader.load(pluginPath.toStdString())) {
            qWarning() << "[main] Failed to load modbus plugin from:" << pluginPath
                       << "— falling back to mock";
        } else {
            qDebug() << "[main] Loaded modbus plugin from:" << pluginPath;
        }
    }

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
