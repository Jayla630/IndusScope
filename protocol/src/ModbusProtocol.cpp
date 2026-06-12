#include "indusscope/protocol/ModbusProtocol.h"
#include "indusscope/protocol/ProtocolFactory.h"
#include "indusscope/protocol/TcpTransport.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace indusscope::protocol {

namespace {

// --- FC03 wire constants FC03 线上常量 ---
constexpr std::uint8_t  kFunctionRead      = 0x03; // read holding registers / 读保持寄存器
constexpr std::uint8_t  kFunctionException = 0x83; // FC03 with high bit set / FC03 高位置 1
constexpr std::size_t   kMbapSize          = 7;    // MBAP header bytes / MBAP 头字节数
constexpr std::size_t   kRequestSize       = 12;   // FC03 request ADU bytes / FC03 请求 ADU 字节数
constexpr std::size_t   kMaxPduSize        = 256;  // PDU buffer capacity, > 2 + 2*125 / PDU 缓冲容量,> 2 + 2*125
constexpr std::uint16_t kMaxQuantity       = 125;  // FC03 spec limit per request / FC03 规范单请求上限

// Manual big-endian 16-bit join/split; htons/ntohs are banned (drag in platform socket headers).
// 手动大端 16 位拼/拆;禁用 htons/ntohs(会拖入平台 socket 头)。
inline void putU16BE(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v & 0xFF);
}
inline std::uint16_t getU16BE(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<unsigned>(p[0]) << 8) | p[1]);
}

} // namespace

ModbusProtocol::ModbusProtocol(std::unique_ptr<ITransport> transport)
    : m_transport(std::move(transport)) {}

std::string ModbusProtocol::name() const { return "modbus"; }

bool ModbusProtocol::configure(const ProtocolConfig& cfg) {
    // Split endpoint at the LAST ':' — covers IPv4/hostname; IPv6 is intentionally out of v1 scope.
    // 按最后一个 ':' 拆 endpoint —— 覆盖 IPv4/域名;IPv6 有意不在 v1 范围。
    const auto pos = cfg.endpoint.rfind(':');
    if (pos != std::string::npos) {
        m_host = cfg.endpoint.substr(0, pos);
        try {
            const unsigned long p = std::stoul(cfg.endpoint.substr(pos + 1));
            if (p >= 1 && p <= 65535) m_port = static_cast<std::uint16_t>(p);
        } catch (...) { /* keep default / 保留默认 */ }
    } else {
        m_host = cfg.endpoint;
    }

    // Parse "unit_id" — default 1, valid range [0,255] / 解析从站单元号,默认 1,合法范围 [0,255]
    auto it = cfg.params.find("unit_id");
    if (it != cfg.params.end()) {
        try {
            const unsigned long v = std::stoul(it->second);
            if (v <= 0xFF) m_unit_id = static_cast<std::uint8_t>(v);
        } catch (...) { /* keep default / 保留默认 */ }
    }

    // Parse "start_address" — default 0, 0-based wire address / 解析起始地址,默认 0,0 基线上地址
    it = cfg.params.find("start_address");
    if (it != cfg.params.end()) {
        try {
            const unsigned long v = std::stoul(it->second);
            if (v <= 0xFFFF) m_start_address = static_cast<std::uint16_t>(v);
        } catch (...) { /* keep default / 保留默认 */ }
    }

    // Parse "quantity" — default 1, clamped to FC03 spec range [1,125] / 解析寄存器数,默认 1,夹到 FC03 规范范围 [1,125]
    it = cfg.params.find("quantity");
    if (it != cfg.params.end()) {
        try {
            const unsigned long v = std::stoul(it->second);
            m_quantity = static_cast<std::uint16_t>(
                std::clamp<unsigned long>(v, 1, kMaxQuantity));
        } catch (...) { /* keep default / 保留默认 */ }
    }

    // Parse "scale" — default 1.0 / 解析换算系数,默认 1.0
    it = cfg.params.find("scale");
    if (it != cfg.params.end()) {
        try { m_scale = std::stod(it->second); }
        catch (...) { /* keep default / 保留默认 */ }
    }

    // Parse "offset" — default 0.0 / 解析偏移量,默认 0.0
    it = cfg.params.find("offset");
    if (it != cfg.params.end()) {
        try { m_offset = std::stod(it->second); }
        catch (...) { /* keep default / 保留默认 */ }
    }

    // Parse "timeout_ms" — default 100, negative rejected / 解析超时毫秒,默认 100,负值拒绝
    it = cfg.params.find("timeout_ms");
    if (it != cfg.params.end()) {
        try {
            const int v = std::stoi(it->second);
            if (v >= 0) m_timeout_ms = v;
        } catch (...) { /* keep default / 保留默认 */ }
    }

    return true;
}

bool ModbusProtocol::open() {
    // Build TcpTransport lazily when no transport was injected (factory-constructed path).
    // 工厂构造路径(无注入 transport)时惰性建立 TcpTransport。
    // Injection path (tests via FakeTransport) is untouched — hermetic tests stay green.
    // 注入路径(测试用 FakeTransport)不变——hermetic 测试仍全绿。
    if (!m_transport)
        m_transport = std::make_unique<TcpTransport>(m_host, m_port, m_timeout_ms);
    return m_transport->open();
}

void ModbusProtocol::close() {
    if (m_transport) m_transport->close();
}

bool ModbusProtocol::isOpen() const {
    return m_transport && m_transport->isOpen();
}

std::size_t ModbusProtocol::channelCount() const { return m_quantity; }

bool ModbusProtocol::recvExact(std::uint8_t* buf, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        const int r = m_transport->recv(buf + got, n - got);
        // 0 = timeout/no data, -1 = error — either way abandon this frame, no spinning.
        // 0 = 超时/无数据,-1 = 错误 —— 一律放弃本帧,绝不空转。
        if (r <= 0) return false;
        got += static_cast<std::size_t>(r);
    }
    return true;
}

std::size_t ModbusProtocol::poll(Reading* out, std::size_t max_n) {
    if (!isOpen() || max_n == 0) return 0;

    // --- Build FC03 request ADU 拼 FC03 请求 ADU ---
    ++m_transaction_id; // uint16 wraps naturally / uint16 自然回绕
    std::array<std::uint8_t, kRequestSize> req{};
    putU16BE(req.data() + 0, m_transaction_id);  // Transaction ID / 事务号
    putU16BE(req.data() + 2, 0x0000);            // Protocol ID, always 0 / 协议号恒 0
    putU16BE(req.data() + 4, 6);                 // Length = bytes after this field / 其后字节数
    req[6] = m_unit_id;                          // Unit ID / 单元号
    req[7] = kFunctionRead;                      // Function 0x03 / 功能码
    putU16BE(req.data() + 8, m_start_address);   // Starting address, 0-based / 起始地址,0 基
    putU16BE(req.data() + 10, m_quantity);       // Quantity / 寄存器数

    if (!m_transport->send(req.data(), req.size())) return 0;

    // --- Read & validate MBAP 读取并校验 MBAP ---
    std::array<std::uint8_t, kMbapSize> mbap{};
    if (!recvExact(mbap.data(), mbap.size())) return 0;

    const std::uint16_t rx_tid   = getU16BE(mbap.data() + 0);
    const std::uint16_t rx_proto = getU16BE(mbap.data() + 2);
    const std::uint16_t rx_len   = getU16BE(mbap.data() + 4);
    // mbap[6] is Unit ID — echoed but not validated in v1 / mbap[6] 为单元号——回显但 v1 不校验

    if (rx_tid != m_transaction_id) return 0;  // response must echo our transaction / 响应必须回显本次事务号
    if (rx_proto != 0x0000) return 0;          // Modbus protocol id is always 0 / Modbus 协议号恒为 0

    // Never trust the wire-supplied length: bound-check BEFORE reading L-1 bytes into the
    // fixed PDU buffer. Minimum legal L = 3 (UnitID + Function + exception code); a hostile
    // L (e.g. 0xFFFF) must never drive recvExact past the buffer capacity.
    // 绝不信任线上长度字段:先做边界校验,再按 L-1 读入定长 PDU 缓冲。
    // 最小合法 L = 3(单元号+功能码+异常码);敌意 L(如 0xFFFF)绝不能让 recvExact 越过缓冲容量。
    if (rx_len < 3 || static_cast<std::size_t>(rx_len) - 1 > kMaxPduSize) return 0;

    const std::size_t pdu_len = static_cast<std::size_t>(rx_len) - 1; // L includes already-read UnitID / L 含已读单元号
    std::array<std::uint8_t, kMaxPduSize> pdu{};
    if (!recvExact(pdu.data(), pdu_len)) return 0;

    // --- Validate PDU 校验 PDU ---
    if (pdu[0] == kFunctionException) return 0;  // exception response: do not parse payload / 异常响应,不解析负载
    if (pdu[0] != kFunctionRead) return 0;       // unexpected function echo / 非预期功能码

    const std::size_t byte_count = pdu[1];
    // Byte count must match what we asked for, AND stay inside the bytes actually received —
    // a lying byte count must not make the decoder read stale buffer contents.
    // byte count 必须与请求数量一致,且不得越过实收字节范围——
    // 谎报的 byte count 不能让解码器读到缓冲里的脏数据。
    if (byte_count != 2 * static_cast<std::size_t>(m_quantity)) return 0;
    if (2 + byte_count > pdu_len) return 0;

    // --- Decode registers 解码寄存器 ---
    // One steady-clock stamp per scan; all N readings of this transaction share it.
    // 每次扫描取一次 steady_clock;本事务的 N 个读数共用该时间戳。
    const std::int64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    const std::size_t n = std::min<std::size_t>(m_quantity, max_n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint16_t raw = getU16BE(pdu.data() + 2 + 2 * i);
        out[i].channel      = static_cast<std::uint32_t>(i);
        // u16 treated as unsigned; signed / 32-bit register pairs are a later extension.
        // u16 按无符号处理;有符号/32 位寄存器对为后续扩展。
        out[i].value        = static_cast<double>(raw) * m_scale + m_offset;
        out[i].timestamp_ns = ts;
    }
    return n;
}

// Self-register "modbus" — inside the namespace so ModbusProtocol is visible as an unqualified name.
// 自注册 "modbus"——在命名空间内调用,使 ModbusProtocol 以非限定名可见。
// Only effective when this TU is linked with LINK_LIBRARY:WHOLE_ARCHIVE (see protocol/tests/CMakeLists.txt).
// 仅当本 TU 以 WHOLE_ARCHIVE 方式链接时生效(参见 protocol/tests/CMakeLists.txt)。
INDUSSCOPE_REGISTER_PROTOCOL("modbus", ModbusProtocol)

} // namespace indusscope::protocol
