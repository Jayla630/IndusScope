#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace indusscope::protocol {

/// Cross-platform dynamic plugin loader.
/// 跨平台动态插件加载器。
///
/// Loads a shared library (.dll/.so), validates the C-ABI contract, and registers
/// the contained protocol with the main program's ProtocolFactory singleton.
/// 加载共享库(.dll/.so),校验 C-ABI 契约,并将其中的协议注册进主程序 ProtocolFactory 单例。
///
/// Successfully loaded library handles are held for the loader's lifetime and never
/// unloaded — live IDeviceProtocol instances may still reference code inside them.
/// 成功加载的库句柄在加载器生命期内持有,永不卸载——
/// 在用的 IDeviceProtocol 实例可能仍引用库中代码。
class PluginLoader {
public:
    PluginLoader()  = default;
    ~PluginLoader() = default;

    // Non-copyable, non-movable: holds raw OS handles.
    // 不可复制、不可移动:持有裸 OS 句柄。
    PluginLoader(const PluginLoader&)            = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;
    PluginLoader(PluginLoader&&)                 = delete;
    PluginLoader& operator=(PluginLoader&&)      = delete;

    /// Load plugin from path; validate ABI version; register with ProtocolFactory.
    /// 从路径加载插件;校验 ABI 版本;向 ProtocolFactory 注册。
    /// Returns false (gracefully, no crash) on:
    ///   load failure | missing export symbols | ABI mismatch | duplicate name
    /// 以下情形返回 false(优雅,不崩溃):
    ///   加载失败 | 缺失导出符号 | ABI 不符 | 名称重复
    bool load(const std::string& path);

private:
#ifdef _WIN32
    std::vector<uintptr_t> m_handles;  // HMODULE stored as uintptr_t to avoid windows.h in header / HMODULE 以 uintptr_t 存储,避免头文件引入 windows.h
#else
    std::vector<void*>     m_handles;  // POSIX dlopen handles / POSIX dlopen 句柄
#endif
};

} // namespace indusscope::protocol
