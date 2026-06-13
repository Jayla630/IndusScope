#include "indusscope/protocol/PluginLoader.h"
#include "indusscope/protocol/PluginApi.h"
#include "indusscope/protocol/IDeviceProtocol.h"
#include "indusscope/protocol/ProtocolFactory.h"

#include <cstdio>
#include <memory>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
// POSIX path — untested until M3 / POSIX 路径——M3 前未实测
#  include <dlfcn.h>
#endif

namespace indusscope::protocol {

bool PluginLoader::load(const std::string& path) {
#ifdef _WIN32

    // --- Windows: LoadLibrary + GetProcAddress ---
    // Windows:LoadLibrary + GetProcAddress
    HMODULE handle = LoadLibraryA(path.c_str());
    if (!handle) {
        std::fprintf(stderr,
            "PluginLoader: LoadLibraryA(\"%s\") failed (error %lu)\n",
            path.c_str(), GetLastError());
        return false;
    }

    // Resolve the three mandatory C-ABI entry points. / 解析三个必须的 C-ABI 导出口。
    const auto fn_abi    = reinterpret_cast<FnAbiVersion>  (GetProcAddress(handle, "indusscope_plugin_abi_version"));
    const auto fn_name   = reinterpret_cast<FnPluginName>  (GetProcAddress(handle, "indusscope_plugin_name"));
    const auto fn_create = reinterpret_cast<FnPluginCreate>(GetProcAddress(handle, "indusscope_plugin_create"));

    if (!fn_abi || !fn_name || !fn_create) {
        std::fprintf(stderr,
            "PluginLoader: \"%s\" is missing required export(s)\n", path.c_str());
        // Rejected before registration — no live instances; safe to unload.
        // 注册前被拒绝——无活实例;可安全卸载。
        FreeLibrary(handle);
        return false;
    }

    // ABI version check: reject stale or future-ABI plugins. / ABI 版本校验:拒绝陈旧或未来 ABI 的插件。
    const int abi = fn_abi();
    if (abi != INDUSSCOPE_PLUGIN_ABI_VERSION) {
        std::fprintf(stderr,
            "PluginLoader: \"%s\" ABI version mismatch (got %d, expected %d)\n",
            path.c_str(), abi, INDUSSCOPE_PLUGIN_ABI_VERSION);
        // Rejected — no live instances; safe to unload. / 被拒绝——无活实例;可安全卸载。
        FreeLibrary(handle);
        return false;
    }

    // Register with the main program's ProtocolFactory singleton.
    // 向主程序 ProtocolFactory 单例注册。
    // Lambda captures fn_create by value — stays valid as long as the handle is alive.
    // Lambda 按值捕获 fn_create——只要句柄存在即有效。
    const char* proto_name = fn_name();
    const bool  ok = ProtocolFactory::instance().registerProtocol(
        proto_name,
        [fn_create] {
            return std::unique_ptr<IDeviceProtocol>(fn_create());
        });

    if (!ok) {
        std::fprintf(stderr,
            "PluginLoader: protocol \"%s\" already registered — duplicate ignored\n",
            proto_name);
        // Duplicate registration: reject cleanly; no live instances from this load.
        // 重名注册:干净拒绝;此次加载无活实例。
        FreeLibrary(handle);
        return false;
    }

    // Registration succeeded — keep handle alive for the loader's lifetime.
    // 注册成功——在加载器生命期内保持句柄存活。
    m_handles.push_back(reinterpret_cast<uintptr_t>(handle));
    return true;

#else
    // POSIX path — untested until M3 / POSIX 路径——M3 前未实测

    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        std::fprintf(stderr,
            "PluginLoader: dlopen(\"%s\") failed: %s\n", path.c_str(), dlerror());
        return false;
    }

    // POSIX path — untested until M3 / POSIX 路径——M3 前未实测
    const auto fn_abi    = reinterpret_cast<FnAbiVersion>  (dlsym(handle, "indusscope_plugin_abi_version"));
    const auto fn_name   = reinterpret_cast<FnPluginName>  (dlsym(handle, "indusscope_plugin_name"));
    const auto fn_create = reinterpret_cast<FnPluginCreate>(dlsym(handle, "indusscope_plugin_create"));

    if (!fn_abi || !fn_name || !fn_create) {
        std::fprintf(stderr,
            "PluginLoader: \"%s\" is missing required export(s)\n", path.c_str());
        // Rejected — no live instances; safe to unload. / 被拒绝——无活实例;可安全卸载。
        dlclose(handle);  // POSIX path — untested until M3 / POSIX 路径——M3 前未实测
        return false;
    }

    const int abi = fn_abi();
    if (abi != INDUSSCOPE_PLUGIN_ABI_VERSION) {
        std::fprintf(stderr,
            "PluginLoader: \"%s\" ABI version mismatch (got %d, expected %d)\n",
            path.c_str(), abi, INDUSSCOPE_PLUGIN_ABI_VERSION);
        // POSIX path — untested until M3 / POSIX 路径——M3 前未实测
        dlclose(handle);
        return false;
    }

    const char* proto_name = fn_name();
    const bool  ok = ProtocolFactory::instance().registerProtocol(
        proto_name,
        [fn_create] {
            return std::unique_ptr<IDeviceProtocol>(fn_create());
        });

    if (!ok) {
        std::fprintf(stderr,
            "PluginLoader: protocol \"%s\" already registered — duplicate ignored\n",
            proto_name);
        // POSIX path — untested until M3 / POSIX 路径——M3 前未实测
        dlclose(handle);
        return false;
    }

    m_handles.push_back(handle);  // POSIX path — untested until M3 / POSIX 路径——M3 前未实测
    return true;
#endif
}

} // namespace indusscope::protocol
