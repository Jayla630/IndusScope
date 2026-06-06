#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "indusscope/protocol/IDeviceProtocol.h"

namespace indusscope::protocol {

/// Meyers-singleton registry mapping protocol name strings to factory functions.
/// Meyers 单例注册表,将协议名字符串映射到工厂函数。
class ProtocolFactory {
public:
    /// Factory function type: no-arg callable returning a new protocol instance.
    /// 工厂函数类型:无参可调用对象,返回新协议实例。
    using Creator = std::function<std::unique_ptr<IDeviceProtocol>()>;

    /// Return the singleton instance (Meyers singleton, thread-safe since C++11).
    /// 返回单例实例(Meyers 单例,C++11 起线程安全初始化)。
    static ProtocolFactory& instance();

    /// Register a protocol by name. First registration wins; empty creator or duplicate name returns false.
    /// 按名注册协议。首注册生效;空 creator 或重名返回 false。
    bool registerProtocol(const std::string& name, Creator creator);

    /// Create a protocol instance by name. Returns nullptr if name is not registered.
    /// 按名创建协议实例。未注册返回 nullptr。
    std::unique_ptr<IDeviceProtocol> create(const std::string& name) const;

    /// Return all registered names in sorted order (std::map key order).
    /// 返回所有已注册名称,按 std::map 键序排列(有序)。
    std::vector<std::string> registeredNames() const;

private:
    ProtocolFactory() = default;
    ProtocolFactory(const ProtocolFactory&)            = delete;
    ProtocolFactory& operator=(const ProtocolFactory&) = delete;
    ProtocolFactory(ProtocolFactory&&)                 = delete;
    ProtocolFactory& operator=(ProtocolFactory&&)      = delete;

    std::map<std::string, Creator> m_creators; // name → factory function / 名称→工厂函数
};

} // namespace indusscope::protocol

/// Self-registration macro. Declares a TU-local static bool that fires registration at startup.
/// 自注册宏。声明 TU 本地静态 bool,程序启动时触发注册。
///
/// CALL SITE must be INSIDE namespace indusscope::protocol so Type is visible as an
/// unqualified name. The ## token-paste cannot include '::'.
/// 调用点【必须】在 namespace indusscope::protocol 内,使 Type 以非限定名可见。
/// ## 不能粘含 '::' 的 token,故调用点必须在命名空间内。
///
/// Analogous to C# [Protocol] attribute + reflection scan, but resolved at static-init time.
/// 类似 C# [Protocol] 特性 + 反射扫描,但在静态初始化期完成。
///
/// NOTE: only effective when this TU is linked with --whole-archive / LINK_LIBRARY:WHOLE_ARCHIVE.
/// 注意:仅当本 TU 被 whole-archive 方式链接时才生效。
#define INDUSSCOPE_REGISTER_PROTOCOL(name_str, Type)                                    \
    namespace {                                                                          \
    const bool g_indusscope_registered_##Type =                                         \
        ::indusscope::protocol::ProtocolFactory::instance().registerProtocol(           \
            (name_str), [] { return std::make_unique<Type>(); });                       \
    }
