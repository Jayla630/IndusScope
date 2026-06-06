#pragma once

#include <cstddef>
#include <cstdint>
#include "indusscope/protocol/IDeviceProtocol.h"

namespace indusscope::protocol {

/// Deterministic in-process mock; no I/O, no wall-clock dependency, no allocations.
/// 确定性进程内模拟;无 I/O、无 wall-clock 依赖、不分配内存。
class MockProtocol final : public IDeviceProtocol {
public:
    MockProtocol() = default;

    std::string name() const override;
    bool        configure(const ProtocolConfig&) override;
    bool        open() override;
    void        close() override;
    bool        isOpen() const override;
    std::size_t channelCount() const override;
    std::size_t poll(Reading* out, std::size_t max_n) override;

private:
    std::size_t   m_channels  {4};         // channel count / 通道数
    std::int64_t  m_period_ns {1'000'000}; // ns per scan step / 每次扫描的纳秒步长
    std::int64_t  m_start_ns  {0};         // base timestamp / 基础时间戳
    bool          m_open      {false};     // open state / 打开状态
    std::uint64_t m_scan_k    {0};         // scan counter, reset on open() / 扫描计数器,open() 时归零
};

} // namespace indusscope::protocol
