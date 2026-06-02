#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>

// Static QML plugin import — required when indusscope_ui is linked statically.
// The plugin class name is URI "IndusScope.Ui" with dots → underscores + "Plugin".
// 静态 QML 插件导入 — indusscope_ui 以静态库链接时必须显式导入。
// 插件类名由 URI "IndusScope.Ui" 将点号替换为下划线并追加 "Plugin" 生成。
Q_IMPORT_QML_PLUGIN(IndusScope_UiPlugin)

// Forward-declare the auto-generated type registration function that
// QML_ELEMENT + qt_add_qml_module produce.  On MinGW static-library builds
// the QQmlModuleRegistration static initializer may be dropped by the linker,
// so we keep a volatile reference to prevent that.
// 前置声明 QML_ELEMENT + qt_add_qml_module 自动生成的类型注册函数。
// MinGW 静态库构建下 QQmlModuleRegistration 静态初始化器可能被链接器剔除,
// 因此用 volatile 引用予以保留。
extern void qml_register_types_IndusScope_Ui();

/// S1.4b — Qt Quick entry point with static CurveController adapter.
/// S1.4b — Qt Quick 入口,含静态 CurveController 适配器。
int main(int argc, char *argv[])
{
    // QGuiApplication is sufficient for Qt Quick scene-graph rendering;
    // QApplication (QtWidgets) is not needed and not linked.
    // QGuiApplication 足够 Qt Quick 场景图渲染;无需 QApplication (QtWidgets),未链接。
    QGuiApplication app(argc, argv);

    // Volatile pointer to the auto-generated type registration function keeps
    // the MinGW static linker from dropping the QQmlModuleRegistration static
    // initializer that lives in the same translation unit.  We do NOT call
    // the function directly — the static initializer handles registration
    // during module import, ensuring types are registered in the correct
    // module-activation context.
    // volatile 指针引用自动生成的类型注册函数,阻止 MinGW 静态链接器剔除
    // 同翻译单元内的 QQmlModuleRegistration 静态初始化器。不直接调用该函数——
    // 静态初始化器在模块导入期间负责注册,确保类型在正确的模块激活上下文中注册。
    volatile auto keepReg = &qml_register_types_IndusScope_Ui;
    Q_UNUSED(keepReg);

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
