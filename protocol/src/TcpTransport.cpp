#include "indusscope/protocol/TcpTransport.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
// POSIX path — untested until M3 / POSIX 路径 M3 前未实测
#  include <arpa/inet.h>
#  include <cerrno>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

#include <cstdio>

namespace indusscope::protocol {

// === WinsockGuard (Windows-only Meyers singleton) ==========================================
// WinsockGuard(仅 Windows 的 Meyers 单例)
#ifdef _WIN32
namespace {
/// Calls WSAStartup once at first use; WSACleanup in destructor.
/// 首次使用时调用一次 WSAStartup;析构时 WSACleanup。
struct WinsockGuard {
    static WinsockGuard& instance() {
        static WinsockGuard g;
        return g;
    }
private:
    WinsockGuard()  { WSAStartup(MAKEWORD(2, 2), &m_data); }
    ~WinsockGuard() { WSACleanup(); }
    WSADATA m_data{};
};
} // namespace
#endif

// === Platform helpers ==============================================================
// 平台工具函数
#ifdef _WIN32
static inline SOCKET    toSock(uintptr_t v)   { return static_cast<SOCKET>(v); }
static inline uintptr_t fromSock(SOCKET s)    { return static_cast<uintptr_t>(s); }
static inline bool      sockInvalid(uintptr_t v) { return v == ~uintptr_t{0}; }
static inline void      closeFd(uintptr_t v)     { ::closesocket(toSock(v)); }
#else
// POSIX path — untested until M3 / POSIX 路径 M3 前未实测
static inline bool sockInvalid(int v) { return v < 0; }
static inline void closeFd(int v)     { ::close(v); }
#endif

// === TcpTransport =================================================================

TcpTransport::TcpTransport(std::string host, std::uint16_t port, int timeout_ms)
    : m_host(std::move(host)), m_port(port), m_timeout_ms(timeout_ms) {}

TcpTransport::~TcpTransport() {
    closeSocket();
}

void TcpTransport::closeSocket() {
    if (!sockInvalid(m_sock)) {
        closeFd(m_sock);
#ifdef _WIN32
        m_sock = ~uintptr_t{0};
#else
        // POSIX path — untested until M3 / POSIX 路径 M3 前未实测
        m_sock = -1;
#endif
    }
    m_open = false;
}

bool TcpTransport::open() {
    // Guard against double-open: unconditional socket() would leak the old handle.
    // 防双开:无条件调 socket() 会泄漏旧句柄。
    if (m_open) return true;

    // --- Winsock init (noop on POSIX) / Winsock 初始化(POSIX 空操作) ---
#ifdef _WIN32
    WinsockGuard::instance(); // ensure WSAStartup before any socket call / 确保首个 socket 调用前 WSAStartup 已执行
    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;
    m_sock = fromSock(sock);
#else
    // POSIX path — untested until M3 / POSIX 路径 M3 前未实测
    m_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_sock < 0) return false;
#endif

    // --- Resolve IPv4 literal (v1; hostname/IPv6 via getaddrinfo is future work) ---
    // 解析 IPv4 字面量(v1;hostname/IPv6 通过 getaddrinfo 留待后续)
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(m_port);  // htons: socket API here, not frame logic / socket API,非帧逻辑
    const int pr = ::inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr);
    if (pr != 1) {
        // pr == 0: not a valid IPv4 literal; pr == -1: unsupported AF
        // pr == 0: 非合法 IPv4 字面量;pr == -1: 不支持的地址族
        std::fprintf(stderr,
            "TcpTransport: inet_pton failed for \"%s\" (IPv4 literal required in v1)\n",
            m_host.c_str());
        closeSocket();
        return false;
    }

    // --- Set non-blocking before connect 连接前设非阻塞 ---
#ifdef _WIN32
    u_long nb = 1;
    if (::ioctlsocket(toSock(m_sock), FIONBIO, &nb) != 0) { closeSocket(); return false; }
#else
    // POSIX path — untested until M3 / POSIX 路径 M3 前未实测
    const int fl = ::fcntl(m_sock, F_GETFL, 0);
    if (fl < 0 || ::fcntl(m_sock, F_SETFL, fl | O_NONBLOCK) < 0) {
        closeSocket(); return false;
    }
#endif

    // --- Non-blocking connect: three outcomes ---
    // 非阻塞 connect:三种结果
    //   == 0              : immediate success (common on POSIX loopback, rare on Windows)
    //                       立即成功(POSIX 回环常见,Windows 少见)
    //   WSAEWOULDBLOCK /
    //   EINPROGRESS / EWOULDBLOCK : pending — wait via select()
    //                               挂起——通过 select() 等待
    //   anything else     : hard failure / 其他:硬失败
    bool needSelect = false;
#ifdef _WIN32
    const int cr = ::connect(toSock(m_sock),
                             reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (cr == 0) {
        needSelect = false; // immediate success / 立即成功
    } else {
        const int cerr = WSAGetLastError();
        if (cerr == WSAEWOULDBLOCK) { needSelect = true; }
        else { closeSocket(); return false; }
    }
#else
    // POSIX path — untested until M3 / POSIX 路径 M3 前未实测
    const int cr = ::connect(m_sock,
                             reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (cr == 0) {
        needSelect = false; // immediate success on loopback / 回环立即成功
    } else {
        if (errno == EINPROGRESS || errno == EWOULDBLOCK) { needSelect = true; }
        else { closeSocket(); return false; }
    }
#endif

    // --- select() wait for writability (skipped when connect returned 0) ---
    // select() 等可写(connect 立即返 0 时跳过)
    if (needSelect) {
        fd_set wset;
        FD_ZERO(&wset);
        timeval tv;
        tv.tv_sec  = static_cast<long>(m_timeout_ms / 1000);
        tv.tv_usec = static_cast<long>((m_timeout_ms % 1000) * 1000);
#ifdef _WIN32
        FD_SET(toSock(m_sock), &wset);
        const int sr = ::select(0, nullptr, &wset, nullptr, &tv); // nfds ignored on Windows / Windows 忽略 nfds
#else
        // POSIX path — untested until M3 / POSIX 路径 M3 前未实测
        FD_SET(m_sock, &wset);
        const int sr = ::select(m_sock + 1, nullptr, &wset, nullptr, &tv);
#endif
        if (sr <= 0) {
            // sr == 0: timeout; sr < 0: error / sr == 0: 超时; sr < 0: 错误
            closeSocket(); return false;
        }

        // select() writable ≠ connected; RST/refused also makes the fd writable.
        // select() 可写 ≠ 已连接;RST/refused 也会让 fd 可写。
        // getsockopt(SO_ERROR) reveals the true connection outcome.
        // getsockopt(SO_ERROR) 揭示真实连接结果。
        int soErr = 0;
#ifdef _WIN32
        int soLen = static_cast<int>(sizeof(soErr));
        if (::getsockopt(toSock(m_sock), SOL_SOCKET, SO_ERROR,
                         reinterpret_cast<char*>(&soErr), &soLen) != 0 || soErr != 0) {
            closeSocket(); return false;
        }
#else
        // POSIX path — untested until M3 / POSIX 路径 M3 前未实测
        socklen_t soLen = sizeof(soErr);
        if (::getsockopt(m_sock, SOL_SOCKET, SO_ERROR, &soErr, &soLen) != 0 || soErr != 0) {
            closeSocket(); return false;
        }
#endif
    }

    // --- Set back to blocking 设回阻塞 ---
#ifdef _WIN32
    u_long bl = 0;
    if (::ioctlsocket(toSock(m_sock), FIONBIO, &bl) != 0) { closeSocket(); return false; }
#else
    // POSIX path — untested until M3 / POSIX 路径 M3 前未实测
    const int fl2 = ::fcntl(m_sock, F_GETFL, 0);
    if (fl2 < 0 || ::fcntl(m_sock, F_SETFL, fl2 & ~O_NONBLOCK) < 0) {
        closeSocket(); return false;
    }
#endif

    // --- SO_RCVTIMEO: enforce recv() deadline / 设 recv() 超时上限 ---
#ifdef _WIN32
    DWORD rcvtmo = static_cast<DWORD>(m_timeout_ms);
    ::setsockopt(toSock(m_sock), SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&rcvtmo), sizeof(rcvtmo));
#else
    // POSIX path — untested until M3 / POSIX 路径 M3 前未实测
    timeval rcvtv;
    rcvtv.tv_sec  = static_cast<long>(m_timeout_ms / 1000);
    rcvtv.tv_usec = static_cast<long>((m_timeout_ms % 1000) * 1000);
    ::setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&rcvtv), sizeof(rcvtv));
#endif

    // --- TCP_NODELAY: disable Nagle to prevent 40 ms latency spikes in req-resp mode ---
    // 关 Nagle,防请求-响应模式下 40ms 延迟毛刺
    int one = 1;
#ifdef _WIN32
    ::setsockopt(toSock(m_sock), IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&one), sizeof(one));
#else
    // POSIX path — untested until M3 / POSIX 路径 M3 前未实测
    ::setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&one), sizeof(one));
#endif

    m_open = true;
    return true;
}

void TcpTransport::close() {
    closeSocket();
}

bool TcpTransport::isOpen() const {
    return m_open;
}

bool TcpTransport::send(const std::uint8_t* data, std::size_t n) {
    if (!m_open) return false;
    std::size_t sent = 0;
    while (sent < n) {
#ifdef _WIN32
        const int r = ::send(toSock(m_sock),
                             reinterpret_cast<const char*>(data + sent),
                             static_cast<int>(n - sent), 0);
#else
        // MSG_NOSIGNAL prevents SIGPIPE when writing to a closed connection (POSIX/Linux).
        // MSG_NOSIGNAL 防止向已关闭连接写时触发 SIGPIPE (POSIX/Linux)。
        // POSIX path — untested until M3 / POSIX 路径 M3 前未实测
        const int r = static_cast<int>(
            ::send(m_sock, data + sent, n - sent, MSG_NOSIGNAL));
#endif
        if (r <= 0) return false;
        sent += static_cast<std::size_t>(r);
    }
    return true;
}

int TcpTransport::recv(std::uint8_t* out, std::size_t max_n) {
    if (!m_open) return -1;
#ifdef _WIN32
    const int r = ::recv(toSock(m_sock),
                         reinterpret_cast<char*>(out),
                         static_cast<int>(max_n), 0);
#else
    // POSIX path — untested until M3 / POSIX 路径 M3 前未实测
    const int r = static_cast<int>(::recv(m_sock, reinterpret_cast<char*>(out), max_n, 0));
#endif
    if (r > 0) return r;
    if (r == 0) {
        // Peer closed connection: return -1 (error), NOT 0 (timeout).
        // 对端关闭连接:返回 -1(错误),而非 0(超时)。
        // If we returned 0, recvExact would spin forever on EOF.
        // 若返回 0,recvExact 会在 EOF 上无限空转。
        return -1;
    }
    // r < 0: distinguish SO_RCVTIMEO timeout from real I/O errors
    // r < 0:区分 SO_RCVTIMEO 超时与真实 I/O 错误
#ifdef _WIN32
    const int err = WSAGetLastError();
    if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) return 0; // SO_RCVTIMEO fired / SO_RCVTIMEO 触发
#else
    // POSIX path — untested until M3 / POSIX 路径 M3 前未实测
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) return 0;
#endif
    return -1; // hard I/O error / 真实 I/O 错误
}

} // namespace indusscope::protocol
