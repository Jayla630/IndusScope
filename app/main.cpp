#include <iostream>

#include "indusscope/protocol/version.h"
#include "indusscope/core/version.h"
#include "indusscope/ui/version.h"
#include "indusscope/core/MockSource.h"

int main() {
    // --- Layer versions / 三层版本 ---
    std::cout << indusscope::protocol::version() << std::endl;
    std::cout << indusscope::core::version() << std::endl;
    std::cout << indusscope::ui::version() << std::endl;

    // --- MockSource headless demo / 模拟源无头演示 ---
    std::cout << "\n[MockSource demo] sample_rate=1000Hz, signal=10Hz sine, duration=1s"
              << std::endl;
    std::cout << "[MockSource 演示] 采样率=1000Hz,信号=10Hz 正弦,时长=1s"
              << std::endl;

    using indusscope::core::MockSource;
    using indusscope::core::MockSourceConfig;
    using indusscope::core::RingBuffer;
    using indusscope::core::SamplePoint;

    MockSourceConfig cfg;
    cfg.sample_rate_hz = 1000.0;
    cfg.signal_config.amplitude = 1.0;
    cfg.signal_config.frequency_hz = 10.0; // signal freq ≠ sample rate / 信号频率 ≠ 采样率
    cfg.signal_config.noise_stddev = 0.1;

    RingBuffer<SamplePoint> sink(2048); // large enough to avoid drops / 足够大,不丢点
    MockSource src(cfg, sink);

    auto stats = src.run_for(std::chrono::seconds(1));

    std::cout << "produced:       " << stats.produced << std::endl;
    std::cout << "dropped:        " << stats.dropped << std::endl;
    std::cout << "elapsed_ms:     " << (stats.elapsed_ns / 1'000'000) << std::endl;
    std::cout << "achieved_rate:  " << stats.achieved_rate_hz << " Hz" << std::endl;

    return 0;
}
