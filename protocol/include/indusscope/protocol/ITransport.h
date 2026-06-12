#pragma once

#include <cstddef>
#include <cstdint>

namespace indusscope::protocol {

/// Pure byte-transport seam between frame logic and platform I/O.
/// 帧逻辑与平台 I/O 之间的纯字节传输接缝。
/// Frame logic depends only on this interface; real sockets live behind it (S2.5c).
/// 帧逻辑只依赖本接口;真实 socket 实现在其后(S2.5c)。
class ITransport {
public:
    virtual ~ITransport() = default;

    /// Open the underlying byte stream.
    /// 打开底层字节流。
    virtual bool open() = 0;

    /// Close the underlying byte stream.
    /// 关闭底层字节流。
    virtual void close() = 0;

    /// Return true if the stream is currently open.
    /// 返回字节流当前是否已打开。
    virtual bool isOpen() const = 0;

    /// Send exactly n bytes; all-or-nothing. Returns false on any failure.
    /// 发送恰好 n 字节;全发完才算成功,任何失败返回 false。
    virtual bool send(const std::uint8_t* data, std::size_t n) = 0;

    /// Receive up to max_n bytes into out. Returns bytes read (may be fewer
    /// than max_n; partial reads allowed), 0 on timeout / no data, -1 on error.
    /// 向 out 接收最多 max_n 字节。返回实际读到的字节数(可少于 max_n,
    /// 允许部分读取),0 = 超时/无数据,-1 = 错误。
    virtual int recv(std::uint8_t* out, std::size_t max_n) = 0;
};

} // namespace indusscope::protocol
