#include "indusscope/core/ProtocolSource.h"
#include "indusscope/protocol/Reading.h"

#include <algorithm>
#include <array>

namespace indusscope::core {

ProtocolSource::ProtocolSource(
    std::unique_ptr<indusscope::protocol::IDeviceProtocol> proto,
    RingBuffer<SamplePoint>& sink,
    const indusscope::protocol::ProtocolConfig& cfg,
    std::uint32_t channel)
    : m_proto(std::move(proto))
    , m_sink(sink)
    , m_cfg(cfg)
    , m_channel(channel)
{}

bool ProtocolSource::start()
{
    m_proto->configure(m_cfg);
    return m_proto->open();
}

std::size_t ProtocolSource::acquire(std::size_t max_n)
{
    // Clamp to scratch capacity — never overflow the stack array in Release builds.
    // 钳制到 scratch 容量——Release 构建中永不溢出栈数组。
    const std::size_t n = std::min(max_n, kMaxPoll);

    std::array<indusscope::protocol::Reading, kMaxPoll> scratch;
    const std::size_t got = m_proto->poll(scratch.data(), n);

    std::size_t pushed = 0;
    for (std::size_t i = 0; i < got; ++i) {
        if (scratch[i].channel == m_channel) {
            const SamplePoint sp{scratch[i].timestamp_ns, scratch[i].value};
            if (m_sink.push(sp)) {
                ++pushed;
                ++m_produced;
            } else {
                ++m_dropped;
            }
        }
    }
    return pushed;
}

void ProtocolSource::stop()
{
    m_proto->close();
}

} // namespace indusscope::core
