#pragma once

#include <map>
#include <string>

namespace indusscope::protocol {

/// Device connection parameters passed to IDeviceProtocol::configure().
/// 传给 IDeviceProtocol::configure() 的设备连接参数。
struct ProtocolConfig {
    std::string endpoint;                       // e.g. "192.168.1.10:502" / "/dev/ttyS0"
    std::map<std::string, std::string> params;  // protocol-specific extras / 协议私有参数
};

} // namespace indusscope::protocol
