#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "indusscope/core/ProtocolSource.h"
#include "indusscope/core/RingBuffer.h"
#include "indusscope/core/SamplePoint.h"
#include "indusscope/protocol/MockProtocol.h"
#include "indusscope/protocol/ProtocolConfig.h"

#include <cmath>
#include <memory>
#include <iomanip>
#include <sstream>

using Catch::Approx;
using namespace indusscope;

// --- Helpers 辅助 ---

// Full-precision double → string (17 significant digits, avoids std::to_string's 6-place truncation).
// 全精度 double → string (17 位有效数字,避免 std::to_string 的 6 位截断)。
static std::string dstr(double v) {
    std::ostringstream os;
    os << std::setprecision(17) << v;
    return os.str();
}

static protocol::ProtocolConfig make_ramp_cfg(std::size_t channels,
                                               std::int64_t period_ns = 1000,
                                               std::int64_t start_ns  = 0)
{
    protocol::ProtocolConfig cfg;
    cfg.params["channels"]  = std::to_string(channels);
    cfg.params["period_ns"] = std::to_string(period_ns);
    cfg.params["start_ns"]  = std::to_string(start_ns);
    return cfg;
}

static protocol::ProtocolConfig make_sine_cfg(double amplitude,
                                               double frequency_hz,
                                               double phase_rad = 0.0,
                                               std::int64_t period_ns = 1'000'000,
                                               std::int64_t start_ns  = 0,
                                               std::size_t channels   = 1)
{
    protocol::ProtocolConfig cfg;
    cfg.params["waveform"]     = "sine";
    cfg.params["channels"]     = std::to_string(channels);
    cfg.params["period_ns"]    = std::to_string(period_ns);
    cfg.params["start_ns"]     = std::to_string(start_ns);
    cfg.params["amplitude"]    = dstr(amplitude);
    cfg.params["frequency_hz"] = dstr(frequency_hz);
    cfg.params["phase_rad"]    = dstr(phase_rad);
    return cfg;
}

// =============================================================================
// ProtocolSource — ramp mode
// =============================================================================

TEST_CASE("ProtocolSource acquire after start ramp channels=1", "[protocol_source]")
{
    // channels=1: each poll() yields exactly 1 Reading for channel 0.
    // channels=1: 每次 poll() 恰好产出 1 个 channel 0 的 Reading。
    core::RingBuffer<core::SamplePoint> ring(16);
    auto cfg = make_ramp_cfg(/*channels=*/1, /*period_ns=*/1000, /*start_ns=*/0);

    core::ProtocolSource src(std::make_unique<protocol::MockProtocol>(), ring, cfg, /*channel=*/0);
    REQUIRE(src.start());

    src.acquire(1);
    src.acquire(1);
    src.acquire(1);

    REQUIRE(src.produced() == 3);
    REQUIRE(src.dropped()  == 0);
    REQUIRE(ring.size()    == 3);

    // Drain and verify timestamp + value (ramp: value = k*1 + 0 = k).
    // 排空并验证时间戳与值 (斜坡: value = k*1 + 0 = k)。
    core::SamplePoint pts[3];
    ring.pop_batch(pts, 3);

    CHECK(pts[0].timestamp_ns == 0);
    CHECK(pts[0].value        == Approx(0.0));

    CHECK(pts[1].timestamp_ns == 1000);
    CHECK(pts[1].value        == Approx(1.0));

    CHECK(pts[2].timestamp_ns == 2000);
    CHECK(pts[2].value        == Approx(2.0));
}

TEST_CASE("ProtocolSource acquire without start returns 0", "[protocol_source]")
{
    core::RingBuffer<core::SamplePoint> ring(16);
    auto cfg = make_ramp_cfg(/*channels=*/1);

    core::ProtocolSource src(std::make_unique<protocol::MockProtocol>(), ring, cfg, 0);

    // Never called start() — poll() returns 0, acquire() must return 0 and not crash.
    // 从未调用 start()——poll() 返回 0,acquire() 必须返回 0 且不崩溃。
    REQUIRE(src.acquire(4) == 0);
    REQUIRE(ring.empty());
    REQUIRE(src.produced() == 0);
    REQUIRE(src.dropped()  == 0);
}

TEST_CASE("ProtocolSource channel filter channels=4 target=0", "[protocol_source]")
{
    // Each poll() returns 4 Readings (ch 0,1,2,3).
    // Filtering for ch 0 → exactly 1 SamplePoint per acquire(4).
    // 每次 poll() 返回 4 个 Reading (ch 0,1,2,3)。
    // 过滤 ch 0 → 每次 acquire(4) 恰好 1 个 SamplePoint。
    core::RingBuffer<core::SamplePoint> ring(16);
    auto cfg = make_ramp_cfg(/*channels=*/4);

    core::ProtocolSource src(std::make_unique<protocol::MockProtocol>(), ring, cfg, /*channel=*/0);
    REQUIRE(src.start());

    const std::size_t pushed = src.acquire(4);

    REQUIRE(pushed == 1);           // 1 out of 4 channels passes the filter / 4 个通道中 1 个通过过滤
    REQUIRE(ring.size() == 1);
    REQUIRE(src.produced() == 1);

    core::SamplePoint sp;
    ring.pop(sp);
    CHECK(sp.value == Approx(0.0)); // ramp: k=0, channels=4, c=0 → 0*4+0=0 / 斜坡
    CHECK(sp.timestamp_ns == 0);
}

TEST_CASE("ProtocolSource ProtocolSource::dropped increments when ring full", "[protocol_source]")
{
    // Tiny ring (capacity 2 after next_pow2); produce 10 → overflow counted in src.dropped().
    // 极小 ring (next_pow2(2)=2);产出 10 → 溢出计入 src.dropped()。
    // Note: this is ProtocolSource::dropped(), NOT RingBuffer::dropped().
    // 注意:这是 ProtocolSource::dropped(),不是 RingBuffer::dropped()。
    core::RingBuffer<core::SamplePoint> ring(2);  // actual capacity = 2 after next_pow2
    auto cfg = make_ramp_cfg(/*channels=*/1);

    core::ProtocolSource src(std::make_unique<protocol::MockProtocol>(), ring, cfg, 0);
    REQUIRE(src.start());

    for (int i = 0; i < 10; ++i)
        src.acquire(1);

    REQUIRE(src.produced() + src.dropped() == 10);
    REQUIRE(src.dropped() > 0);
    REQUIRE(src.produced() == ring.capacity()); // exactly capacity items pushed before full
                                                 // 恰好 capacity 个元素在满之前推入
}

// =============================================================================
// MockProtocol — sine mode
// =============================================================================

TEST_CASE("MockProtocol sine value bounded by amplitude", "[mock_protocol_sine]")
{
    auto cfg = make_sine_cfg(/*amplitude=*/2.5, /*frequency_hz=*/10.0);

    protocol::MockProtocol proto;
    REQUIRE(proto.configure(cfg));
    REQUIRE(proto.open());

    constexpr double eps = 1e-9;
    protocol::Reading r;
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(proto.poll(&r, 1) == 1);
        CHECK(std::abs(r.value) <= 2.5 + eps);
    }
}

TEST_CASE("MockProtocol sine known value at t=0 with phase_rad=pi/3", "[mock_protocol_sine]")
{
    // At k=0: ts = start_ns = 0, t = 0 s
    // value = amplitude * sin(kTwoPi * frequency_hz * 0 + phase_rad)
    //       = 3.0 * sin(phase_rad)
    // k=0 时: ts = start_ns = 0, t = 0 s
    // value = 3.0 * sin(phase_rad)
    constexpr double amplitude    = 3.0;
    constexpr double frequency_hz = 100.0;
    constexpr double phase_rad    = 1.0471975511965976; // pi/3 / 圆周率除以 3

    auto cfg = make_sine_cfg(amplitude, frequency_hz, phase_rad,
                             /*period_ns=*/1'000'000, /*start_ns=*/0);

    protocol::MockProtocol proto;
    REQUIRE(proto.configure(cfg));
    REQUIRE(proto.open());

    protocol::Reading r;
    REQUIRE(proto.poll(&r, 1) == 1);

    const double expected = amplitude * std::sin(phase_rad); // t=0, sin(0*...) = 0
    CHECK(r.value        == Approx(expected).epsilon(1e-9));
    CHECK(r.timestamp_ns == 0);
}

TEST_CASE("MockProtocol sine known value at non-zero timestamp", "[mock_protocol_sine]")
{
    // k=1: ts = start_ns + 1*period_ns = 0 + 1'000'000 = 1'000'000 ns = 1 ms = 0.001 s
    // value = 1.0 * sin(2π * 50.0 * 0.001 + 0.0) = sin(0.1π) = sin(π/10)
    // k=1 时: ts = 1 ms, t = 0.001 s
    constexpr double kTwoPi       = 6.283185307179586;
    constexpr double amplitude    = 1.0;
    constexpr double frequency_hz = 50.0;
    constexpr double phase_rad    = 0.0;
    constexpr std::int64_t period_ns = 1'000'000; // 1 ms / 1 毫秒

    auto cfg = make_sine_cfg(amplitude, frequency_hz, phase_rad, period_ns, /*start_ns=*/0);

    protocol::MockProtocol proto;
    REQUIRE(proto.configure(cfg));
    REQUIRE(proto.open());

    protocol::Reading r0, r1;
    proto.poll(&r0, 1); // k=0: ts=0
    proto.poll(&r1, 1); // k=1: ts=1'000'000 ns

    CHECK(r1.timestamp_ns == period_ns);
    const double t        = static_cast<double>(r1.timestamp_ns) * 1e-9;
    const double expected = amplitude * std::sin(kTwoPi * frequency_hz * t + phase_rad);
    CHECK(r1.value == Approx(expected).epsilon(1e-9));
}

TEST_CASE("MockProtocol default configure preserves ramp behavior", "[mock_protocol_sine]")
{
    // Default configure (no params) must remain ramp — 14 existing ramp tests rely on this.
    // 默认 configure(无参数) 必须保持斜坡模式——14 个现有斜坡测试依赖此行为。
    protocol::ProtocolConfig cfg; // empty params / 空参数
    protocol::MockProtocol proto;
    REQUIRE(proto.configure(cfg));
    REQUIRE(proto.open());

    protocol::Reading r;
    proto.poll(&r, 1);
    // channels=4 (default), k=0, c=0 → value = 0*4+0 = 0
    // channels=4 (默认), k=0, c=0 → value = 0
    CHECK(r.value == Approx(0.0));
    CHECK(r.channel == 0u);
}

// =============================================================================
// ProtocolSource — sine mode via ProtocolSource adapter
// =============================================================================

TEST_CASE("ProtocolSource sine mode delivers bounded values", "[protocol_source]")
{
    core::RingBuffer<core::SamplePoint> ring(4096);
    auto cfg = make_sine_cfg(/*amplitude=*/1.0, /*frequency_hz=*/10.0,
                              /*phase_rad=*/0.0, /*period_ns=*/200'000,
                              /*start_ns=*/0, /*channels=*/1);

    core::ProtocolSource src(std::make_unique<protocol::MockProtocol>(), ring, cfg, 0);
    REQUIRE(src.start());

    for (int i = 0; i < 100; ++i)
        src.acquire(1);

    REQUIRE(src.produced() == 100);

    core::SamplePoint buf[100];
    const std::size_t drained = ring.pop_batch(buf, 100);
    REQUIRE(drained == 100);

    for (std::size_t i = 0; i < drained; ++i)
        CHECK(std::abs(buf[i].value) <= 1.0 + 1e-9);
}
