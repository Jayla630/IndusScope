#pragma once

#include <cstddef>
#include <string>
#include "indusscope/protocol/Reading.h"
#include "indusscope/protocol/ProtocolConfig.h"

namespace indusscope::protocol {

/// Abstract device protocol contract. All concrete protocols implement this interface.
/// 设备协议抽象契约。所有具体协议实现此接口。
class IDeviceProtocol {
public:
    virtual ~IDeviceProtocol() = default;

    /// Protocol identifier string.
    /// 协议标识字符串。
    virtual std::string name() const = 0;

    /// Store connection parameters; no I/O occurs here.
    /// 存储连接参数;此处不发生 I/O。
    virtual bool configure(const ProtocolConfig&) = 0;

    /// Open the device connection; real implementations may block.
    /// 打开设备连接;真实实现可能阻塞。
    virtual bool open() = 0;

    /// Close the device connection.
    /// 关闭设备连接。
    virtual void close() = 0;

    /// Return true if the connection is currently open.
    /// 返回连接是否当前已打开。
    virtual bool isOpen() const = 0;

    /// Return the number of logical channels this protocol exposes.
    /// 返回此协议暴露的逻辑通道数。
    virtual std::size_t channelCount() const = 0;

    /// Pull up to max_n readings into caller-provided buffer; return actual count written.
    /// 向调用方缓冲区拉取最多 max_n 个读数,返回实际写入数。
    /// Returns 0 if not open. Never allocates internally.
    /// 未 open 返回 0。内部不分配内存。
    virtual std::size_t poll(Reading* out, std::size_t max_n) = 0;
};

} // namespace indusscope::protocol
