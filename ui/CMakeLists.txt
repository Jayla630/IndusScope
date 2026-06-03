add_library(indusscope_ui STATIC
    src/version.cpp
)
add_library(IndusScope::ui ALIAS indusscope_ui)

target_include_directories(indusscope_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/indusscope/ui
)

target_link_libraries(indusscope_ui
    PUBLIC
        IndusScope::core
        Qt6::Core
        Qt6::Quick
)

# --- QML module / QML 模块 ---
# Attach a QML module to the static ui library so Main.qml is bundled
# as a compiled-in resource and loadable via engine.loadFromModule().
# 将 QML 模块挂到静态 ui 库,使 Main.qml 作为编译期资源打包,
# 并通过 engine.loadFromModule() 加载。
qt_add_qml_module(indusscope_ui
    URI "IndusScope.Ui"
    VERSION 1.0
    QML_FILES qml/Main.qml
    SOURCES
        include/indusscope/ui/AcquisitionWorker.h
        src/AcquisitionWorker.cpp
        include/indusscope/ui/CurveController.h
        src/CurveController.cpp
    DEPENDENCIES
        QtQuick
        QtGraphs
    NO_PLUGIN_OPTIONAL
)
