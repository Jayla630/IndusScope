#include "indusscope/protocol/MockProtocol.h"
#include "indusscope/protocol/ProtocolFactory.h"

#include <algorithm>
#include <cmath>

namespace indusscope::protocol {

std::string MockProtocol::name() const { return "mock"; }

bool MockProtocol::configure(const ProtocolConfig& cfg) {
    // Parse "channels" — default 4, clamp >= 1 to prevent degenerate zero-channel state
    // 解析通道数,默认 4,至少 1,防止零通道退化
    auto it = cfg.params.find("channels");
    if (it != cfg.params.end()) {
        try {
            std::size_t v = std::stoul(it->second);
            m_channels = v >= 1 ? v : 1;
        } catch (...) { /* keep default / 保留默认 */ }
    }

    // Parse "period_ns" — default 1,000,000 / 解析时间步长,默认 1,000,000 ns
    it = cfg.params.find("period_ns");
    if (it != cfg.params.end()) {
        try { m_period_ns = std::stoll(it->second); }
        catch (...) { /* keep default / 保留默认 */ }
    }

    // Parse "start_ns" — default 0 / 解析起始时间戳,默认 0
    it = cfg.params.find("start_ns");
    if (it != cfg.params.end()) {
        try { m_start_ns = std::stoll(it->second); }
        catch (...) { /* keep default / 保留默认 */ }
    }

    // Parse "waveform" — "ramp" (default) or "sine" / 解析波形模式,默认 ramp
    it = cfg.params.find("waveform");
    if (it != cfg.params.end()) {
        m_waveform = (it->second == "sine") ? Waveform::Sine : Waveform::Ramp;
    }

    // Parse "amplitude" — default 1.0 / 解析正弦幅值,默认 1.0
    it = cfg.params.find("amplitude");
    if (it != cfg.params.end()) {
        try { m_amplitude = std::stod(it->second); }
        catch (...) { /* keep default / 保留默认 */ }
    }

    // Parse "frequency_hz" — default 10.0 / 解析正弦频率,默认 10.0 Hz
    it = cfg.params.find("frequency_hz");
    if (it != cfg.params.end()) {
        try { m_frequency_hz = std::stod(it->second); }
        catch (...) { /* keep default / 保留默认 */ }
    }

    // Parse "phase_rad" — default 0.0 / 解析正弦相位,默认 0.0 rad
    it = cfg.params.find("phase_rad");
    if (it != cfg.params.end()) {
        try { m_phase_rad = std::stod(it->second); }
        catch (...) { /* keep default / 保留默认 */ }
    }

    return true;
}

bool MockProtocol::open() {
    m_open   = true;
    m_scan_k = 0; // reset scan counter on each open / 每次 open 重置扫描计数器
    return true;
}

void MockProtocol::close() { m_open = false; }

bool MockProtocol::isOpen() const { return m_open; }

std::size_t MockProtocol::channelCount() const { return m_channels; }

std::size_t MockProtocol::poll(Reading* out, std::size_t max_n) {
    if (!m_open) return 0;

    // 2π — avoids non-standard M_PI (not in C++ standard, absent on MSVC without _USE_MATH_DEFINES)
    // 2π——避开非标准 M_PI(不在 C++ 标准中,MSVC 不定义 _USE_MATH_DEFINES 时不存在)
    constexpr double kTwoPi = 6.283185307179586;

    std::size_t n   = std::min(m_channels, max_n);
    std::int64_t ts = m_start_ns + static_cast<std::int64_t>(m_scan_k) * m_period_ns;

    for (std::size_t c = 0; c < n; ++c) {
        out[c].channel      = static_cast<std::uint32_t>(c);
        out[c].timestamp_ns = ts;
        if (m_waveform == Waveform::Sine) {
            const double t = static_cast<double>(ts) * 1e-9; // ns → s / 纳秒转秒
            out[c].value = m_amplitude * std::sin(kTwoPi * m_frequency_hz * t + m_phase_rad);
        } else {
            // Ramp: value = k*channels + c — deterministic, always finite, monotone across scans
            // 斜坡:value = k*channels + c — 确定性、永远有限、跨扫描单调
            out[c].value = static_cast<double>(m_scan_k * m_channels + c);
        }
    }

    ++m_scan_k;
    return n;
}

// Self-register "mock" — called inside the namespace so MockProtocol is visible as an unqualified name.
// 自注册 "mock"——在命名空间内调用,使 MockProtocol 以非限定名可见。
// Only effective when this TU is linked with LINK_LIBRARY:WHOLE_ARCHIVE (see protocol/tests/CMakeLists.txt).
// 仅当本 TU 以 WHOLE_ARCHIVE 方式链接时生效(参见 protocol/tests/CMakeLists.txt)。
INDUSSCOPE_REGISTER_PROTOCOL("mock", MockProtocol)

} // namespace indusscope::protocol
