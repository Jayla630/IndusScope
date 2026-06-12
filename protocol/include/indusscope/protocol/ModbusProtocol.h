#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "indusscope/protocol/IDeviceProtocol.h"
#include "indusscope/protocol/ITransport.h"

namespace indusscope::protocol {

/// Modbus TCP FC03 (read holding registers) client; frame logic only, transport injected.
/// Modbus TCP FC03(读保持寄存器)客户端;纯帧逻辑,传输层注入。
/// Hermetic by construction: depends on ITransport only, never on sockets or platform headers.
/// 构造即 hermetic:只依赖 ITransport,不依赖 socket 或任何平台头。
class ModbusProtocol final : public IDeviceProtocol {
public:
    /// Factory constructor: no transport, open() returns false (TcpTransport arrives in S2.5c).
    /// 工厂用构造:无 transport,open() 返回 false(真 TcpTransport 留待 S2.5c)。
    ModbusProtocol() = default;

    /// Injection constructor for tests and future transport wiring.
    /// 注入构造,供测试与后续传输层接线使用。
    explicit ModbusProtocol(std::unique_ptr<ITransport> transport);

    std::string name() const override;
    bool        configure(const ProtocolConfig&) override;
    bool        open() override;
    void        close() override;
    bool        isOpen() const override;
    std::size_t channelCount() const override;
    std::size_t poll(Reading* out, std::size_t max_n) override;

private:
    /// Loop recv() until exactly n bytes arrive; false on timeout (0) or error (-1).
    /// 循环 recv() 直到恰好 n 字节;超时(0)或错误(-1)返回 false。
    bool recvExact(std::uint8_t* buf, std::size_t n);

    std::unique_ptr<ITransport> m_transport;  // injected byte transport / 注入的字节传输

    std::string   m_host;               // parsed from endpoint, for S2.5c TcpTransport / 从 endpoint 拆出,供 S2.5c 用
    std::uint16_t m_port{502};          // Modbus TCP default port / Modbus TCP 默认端口
    std::uint8_t  m_unit_id{1};         // slave unit identifier / 从站单元号
    std::uint16_t m_start_address{0};   // 0-based wire address / 0 基线上地址
    std::uint16_t m_quantity{1};        // registers per scan = channel count, clamped [1,125] / 每扫描寄存器数=通道数,夹 [1,125]
    double        m_scale{1.0};         // value = raw * scale + offset / 工程量换算系数
    double        m_offset{0.0};        // engineering offset / 工程量偏移
    int           m_timeout_ms{100};    // recv timeout for future TcpTransport / 供未来 TcpTransport 的接收超时
    std::uint16_t m_transaction_id{0};  // incremented per request, response must echo / 每请求递增,响应必须回显
};

} // namespace indusscope::protocol
