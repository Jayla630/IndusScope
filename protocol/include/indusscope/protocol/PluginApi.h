#pragma once

/// ABI version stamp — PluginLoader rejects plugins whose version != this constant.
/// ABI 版本戳——PluginLoader 拒绝版本号不匹配的插件。
/// Increment when any change to the C export contract would break binary compatibility.
/// 当 C 导出契约发生任何破坏二进制兼容性的变更时递增。
#define INDUSSCOPE_PLUGIN_ABI_VERSION 1

/// Symbol-export macro for plugin entry points.
/// 插件导出口的符号导出宏。
/// Windows: __declspec(dllexport); POSIX: default ELF visibility.
/// Windows:__declspec(dllexport);POSIX:默认 ELF 可见性。
#ifdef _WIN32
#  define PLUGIN_EXPORT __declspec(dllexport)
#else
#  define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace indusscope::protocol { class IDeviceProtocol; }

// Function-pointer typedefs used by PluginLoader to cast dlsym/GetProcAddress results.
// 供 PluginLoader 转换 dlsym/GetProcAddress 结果的函数指针 typedef。

/// Returns INDUSSCOPE_PLUGIN_ABI_VERSION compiled into the plugin.
/// 返回编译进插件的 INDUSSCOPE_PLUGIN_ABI_VERSION。
using FnAbiVersion = int (*)();

/// Returns the protocol name string (e.g. "modbus"); lifetime = plugin library.
/// 返回协议名字符串(如 "modbus");生命期 = 插件库。
using FnPluginName = const char* (*)();

/// Allocates and returns a new IDeviceProtocol on the heap; caller wraps in unique_ptr.
/// 在堆上分配并返回新 IDeviceProtocol;调用方包进 unique_ptr。
/// Ownership contract: plugin and host MUST share the same heap (same toolchain + CRT).
/// 所有权约定:插件与宿主必须共享同一堆(相同工具链 + 运行时)。
using FnPluginCreate = indusscope::protocol::IDeviceProtocol* (*)();
