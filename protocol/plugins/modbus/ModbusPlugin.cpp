/// ModbusPlugin.cpp — C-ABI plugin entry points for the Modbus TCP protocol.
/// ModbusPlugin.cpp —— Modbus TCP 协议的 C-ABI 插件导出口。
///
/// Three functions with C linkage and PLUGIN_EXPORT visibility form the ABI contract
/// that PluginLoader resolves via dlsym/GetProcAddress.
/// 三个具有 C 链接和 PLUGIN_EXPORT 可见性的函数构成 ABI 契约,由 PluginLoader 通过
/// dlsym/GetProcAddress 解析。

#include <memory>
#include "indusscope/protocol/PluginApi.h"
#include "indusscope/protocol/ModbusProtocol.h"

using indusscope::protocol::IDeviceProtocol;
using indusscope::protocol::ModbusProtocol;

extern "C" {

/// Returns the ABI version compiled into this plugin.
/// 返回编译进本插件的 ABI 版本号。
PLUGIN_EXPORT int indusscope_plugin_abi_version() {
    return INDUSSCOPE_PLUGIN_ABI_VERSION;
}

/// Returns the protocol name that this plugin registers.
/// 返回本插件所注册的协议名。
PLUGIN_EXPORT const char* indusscope_plugin_name() {
    return "modbus";
}

/// Returns a new ModbusProtocol, transferring ownership across the C boundary.
/// 返回新建的 ModbusProtocol,所有权经 C 边界移交。
/// make_unique().release() keeps construction RAII-safe while satisfying the raw-pointer ABI.
/// make_unique().release() 使构造保持 RAII 安全,同时满足 C ABI 裸指针要求。
/// Caller (PluginLoader) wraps the result in unique_ptr<IDeviceProtocol>.
/// 调用方(PluginLoader)将返回值包进 unique_ptr<IDeviceProtocol>。
/// INVARIANT: plugin and host must share the same heap (same toolchain + CRT).
/// 不变量:插件与宿主必须共享同一堆(相同工具链 + 运行时)。
PLUGIN_EXPORT IDeviceProtocol* indusscope_plugin_create() {
    return std::make_unique<ModbusProtocol>().release();
}

} // extern "C"
