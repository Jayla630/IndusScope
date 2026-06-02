cmake_minimum_required(VERSION 3.21)
project(IndusScope
    VERSION 0.1.0
    DESCRIPTION "Cross-platform industrial device real-time monitoring"
    LANGUAGES CXX
)

# --- C++17 ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Testing ---
include(CTest)

# --- Dependencies ---
# Qt6: resolved at top level so all subdirectories see the targets
find_package(Qt6 REQUIRED COMPONENTS Core Quick)
# Enable AUTOMOC — required by qt_add_qml_module for generated plugin stubs.
# 启用 AUTOMOC — qt_add_qml_module 生成的插件桩需要 MOC 处理。
qt_standard_project_setup()
# QTP0001 NEW: place QML modules at :/qt/qml/ so loadFromModule can discover them.
# QTP0001 NEW: 将 QML 模块放在 :/qt/qml/,使 loadFromModule 可自动发现模块。
qt_policy(SET QTP0001 NEW)

# Catch2: fetched only when testing is enabled
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

# --- Layers (bottom-up: protocol → core → ui → app) ---
add_subdirectory(protocol)
add_subdirectory(core)
add_subdirectory(ui)
add_subdirectory(app)
