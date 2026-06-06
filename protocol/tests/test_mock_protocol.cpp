#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <array>
#include "indusscope/protocol/MockProtocol.h"

using indusscope::protocol::MockProtocol;
using indusscope::protocol::ProtocolConfig;
using indusscope::protocol::Reading;

TEST_CASE("MockProtocol::name() returns \"mock\"", "[protocol][mock]") {
    MockProtocol p;
    REQUIRE(p.name() == "mock");
}

TEST_CASE("MockProtocol::configure() default params", "[protocol][mock]") {
    MockProtocol p;
    REQUIRE(p.configure(ProtocolConfig{}));
    REQUIRE(p.channelCount() == 4);
}

TEST_CASE("MockProtocol::configure() custom channel count", "[protocol][mock]") {
    MockProtocol p;
    ProtocolConfig cfg;
    cfg.params["channels"] = "8";
    REQUIRE(p.configure(cfg));
    REQUIRE(p.channelCount() == 8);
}

TEST_CASE("MockProtocol::configure() non-numeric value does not crash, keeps default", "[protocol][mock]") {
    MockProtocol p;
    ProtocolConfig cfg;
    cfg.params["channels"]  = "abc";
    cfg.params["period_ns"] = "not-a-number";
    cfg.params["start_ns"]  = "??";
    REQUIRE(p.configure(cfg)); // must not throw / 不得抛出
    REQUIRE(p.channelCount() == 4); // default preserved / 保留默认值
}

TEST_CASE("MockProtocol::configure() channels=0 clamps to 1", "[protocol][mock]") {
    MockProtocol p;
    ProtocolConfig cfg;
    cfg.params["channels"] = "0";
    REQUIRE(p.configure(cfg));
    REQUIRE(p.channelCount() >= 1);
}

TEST_CASE("MockProtocol poll before open returns 0", "[protocol][mock]") {
    MockProtocol p;
    p.configure(ProtocolConfig{});
    std::array<Reading, 8> buf{};
    REQUIRE(p.poll(buf.data(), buf.size()) == 0);
}

TEST_CASE("MockProtocol open/close state", "[protocol][mock]") {
    MockProtocol p;
    p.configure(ProtocolConfig{});
    REQUIRE_FALSE(p.isOpen());
    REQUIRE(p.open());
    REQUIRE(p.isOpen());
    p.close();
    REQUIRE_FALSE(p.isOpen());
}

TEST_CASE("MockProtocol poll after open returns channelCount readings", "[protocol][mock]") {
    MockProtocol p;
    ProtocolConfig cfg;
    cfg.params["channels"] = "4";
    p.configure(cfg);
    p.open();

    std::array<Reading, 8> buf{};
    std::size_t n = p.poll(buf.data(), buf.size());
    REQUIRE(n == 4);

    for (std::size_t i = 0; i < n; ++i) {
        REQUIRE(buf[i].channel < 4);
    }
}

TEST_CASE("MockProtocol poll respects max_n buffer cap", "[protocol][mock]") {
    MockProtocol p;
    ProtocolConfig cfg;
    cfg.params["channels"] = "6";
    p.configure(cfg);
    p.open();

    std::array<Reading, 3> buf{};
    std::size_t n = p.poll(buf.data(), buf.size());
    REQUIRE(n == 3); // max_n < channelCount / 上界被尊重
}

TEST_CASE("MockProtocol timestamps are monotonically non-decreasing across polls", "[protocol][mock]") {
    MockProtocol p;
    p.configure(ProtocolConfig{});
    p.open();

    std::array<Reading, 4> buf{};
    std::int64_t prev_ts = std::numeric_limits<std::int64_t>::min();

    for (int scan = 0; scan < 5; ++scan) {
        std::size_t n = p.poll(buf.data(), buf.size());
        REQUIRE(n > 0);
        for (std::size_t i = 0; i < n; ++i) {
            REQUIRE(buf[i].timestamp_ns >= prev_ts);
        }
        prev_ts = buf[0].timestamp_ns;
    }
}

TEST_CASE("MockProtocol values are all finite", "[protocol][mock]") {
    MockProtocol p;
    p.configure(ProtocolConfig{});
    p.open();

    std::array<Reading, 4> buf{};
    for (int scan = 0; scan < 10; ++scan) {
        p.poll(buf.data(), buf.size());
        for (std::size_t i = 0; i < 4; ++i) {
            REQUIRE(std::isfinite(buf[i].value));
        }
    }
}

TEST_CASE("MockProtocol is deterministic: two instances produce identical sequences", "[protocol][mock]") {
    ProtocolConfig cfg;
    cfg.params["channels"]  = "3";
    cfg.params["period_ns"] = "500000";
    cfg.params["start_ns"]  = "100";

    MockProtocol a, b;
    a.configure(cfg);
    b.configure(cfg);
    a.open();
    b.open();

    std::array<Reading, 3> bufa{}, bufb{};
    for (int scan = 0; scan < 5; ++scan) {
        a.poll(bufa.data(), bufa.size());
        b.poll(bufb.data(), bufb.size());
        for (std::size_t i = 0; i < 3; ++i) {
            REQUIRE(bufa[i].channel      == bufb[i].channel);
            REQUIRE(bufa[i].value        == bufb[i].value);
            REQUIRE(bufa[i].timestamp_ns == bufb[i].timestamp_ns);
        }
    }
}

TEST_CASE("MockProtocol close-open replay is identical to first run", "[protocol][mock]") {
    MockProtocol p;
    ProtocolConfig cfg;
    cfg.params["channels"] = "2";
    p.configure(cfg);

    auto run = [&]() {
        p.open();
        std::array<Reading, 2> buf{};
        std::array<Reading, 4> captured{};
        for (int k = 0; k < 2; ++k) {
            p.poll(buf.data(), buf.size());
            captured[k * 2 + 0] = buf[0];
            captured[k * 2 + 1] = buf[1];
        }
        p.close();
        return captured;
    };

    auto first  = run();
    auto second = run();

    for (std::size_t i = 0; i < first.size(); ++i) {
        REQUIRE(first[i].channel      == second[i].channel);
        REQUIRE(first[i].value        == second[i].value);
        REQUIRE(first[i].timestamp_ns == second[i].timestamp_ns);
    }
}

TEST_CASE("MockProtocol start_ns and period_ns are reflected in timestamps", "[protocol][mock]") {
    MockProtocol p;
    ProtocolConfig cfg;
    cfg.params["channels"]  = "1";
    cfg.params["period_ns"] = "1000";
    cfg.params["start_ns"]  = "5000";
    p.configure(cfg);
    p.open();

    Reading r{};
    p.poll(&r, 1); // scan 0: timestamp = 5000 + 0*1000 / 第 0 次:5000
    REQUIRE(r.timestamp_ns == 5000);

    p.poll(&r, 1); // scan 1: timestamp = 5000 + 1*1000 / 第 1 次:6000
    REQUIRE(r.timestamp_ns == 6000);

    p.poll(&r, 1); // scan 2: timestamp = 5000 + 2*1000 / 第 2 次:7000
    REQUIRE(r.timestamp_ns == 7000);
}
