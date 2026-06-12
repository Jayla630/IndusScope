#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "indusscope/protocol/ITransport.h"

namespace indusscope::protocol {

/// TCP socket transport implementing ITransport.
/// TCP socket 传输,实现 ITransport。
/// Wraps Winsock on Windows; BSD sockets on POSIX (POSIX path untested until M3).
/// Windows 封装 Winsock;POSIX 封装 BSD socket(POSIX 路径 M3 前未实测)。
/// IPv4 literal addresses only in v1; hostname/IPv6 (getaddrinfo) deferred to future.
/// v1 仅支持 IPv4 字面量;hostname/IPv6(getaddrinfo)留待后续。
class TcpTransport final : public ITransport {
public:
    TcpTransport(std::string host, std::uint16_t port, int timeout_ms = 100);
    ~TcpTransport() override;

    /// Non-blocking connect + select + SO_ERROR; sets SO_RCVTIMEO + TCP_NODELAY on success.
    /// 非阻塞 connect + select + SO_ERROR;成功后设 SO_RCVTIMEO + TCP_NODELAY。
    bool open() override;

    /// Close the socket; idempotent.
    /// 关闭 socket;幂等。
    void close() override;

    bool isOpen() const override;

    /// Loop-send until all n bytes are sent; MSG_NOSIGNAL on POSIX/Linux.
    /// 循环发送直到 n 字节全部送出;POSIX/Linux 使用 MSG_NOSIGNAL。
    bool send(const std::uint8_t* data, std::size_t n) override;

    /// Return: bytes > 0; 0 = timeout (SO_RCVTIMEO); -1 = error or peer close.
    /// 返回:字节数 > 0;0 = 超时(SO_RCVTIMEO);-1 = 错误或对端关闭。
    int recv(std::uint8_t* out, std::size_t max_n) override;

private:
    /// Close and invalidate the socket handle; safe to call multiple times.
    /// 关闭并置 socket 句柄为无效;可多次调用。
    void closeSocket();

    std::string   m_host;
    std::uint16_t m_port;
    int           m_timeout_ms;
    bool          m_open{false};

    // SOCKET on Windows is UINT_PTR (= uintptr_t); store as uintptr_t so winsock2.h
    // stays out of this header. TcpTransport.cpp casts back to SOCKET before calling winsock.
    // Windows 的 SOCKET 即 UINT_PTR(= uintptr_t);用 uintptr_t 存储使 winsock2.h
    // 不暴露到本头文件中。TcpTransport.cpp 调用 winsock 前强制转回 SOCKET。
#ifdef _WIN32
    uintptr_t m_sock{~uintptr_t{0}};  // ~0 == INVALID_SOCKET / ~0 即 INVALID_SOCKET
#else
    int m_sock{-1};  // POSIX fd; untested until M3 / POSIX 文件描述符;M3 前未实测
#endif
};

} // namespace indusscope::protocol
