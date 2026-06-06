#include <catch2/catch_test_macros.hpp>
#include <array>
#include "indusscope/protocol/ProtocolFactory.h"
// NOTE: MockProtocol.h is intentionally NOT included here.
// 注意:此处故意不 include MockProtocol.h。
// Whole-archive sentinel: if MockProtocol.o is not pulled in by WHOLE_ARCHIVE,
// "mock" will not be registered and tests 1-4 fail — proving the sentinel works.
// whole-archive 哨兵:若 MockProtocol.o 未被 WHOLE_ARCHIVE 拉入,
// "mock" 不会注册,测试 1-4 会失败——从而证明哨兵有效。

using indusscope::protocol::IDeviceProtocol;
using indusscope::protocol::ProtocolConfig;
using indusscope::protocol::ProtocolFactory;
using indusscope::protocol::Reading;

namespace {

// Local stub used for manual-registration tests only; not auto-registered.
// 本地存根,仅用于手动注册测试;不自动注册。
class DummyProtocol final : public IDeviceProtocol {
public:
    std::string name() const override { return "dummy"; }
    bool configure(const ProtocolConfig&) override { return true; }
    bool open() override { return true; }
    void close() override {}
    bool isOpen() const override { return false; }
    std::size_t channelCount() const override { return 0; }
    std::size_t poll(Reading*, std::size_t) override { return 0; }
};

} // anonymous namespace

TEST_CASE("ProtocolFactory registeredNames contains mock (whole-archive sentinel)", "[protocol][factory]") {
    // If this fails: MockProtocol.o was not linked — check LINK_LIBRARY:WHOLE_ARCHIVE.
    // 若此处失败:MockProtocol.o 未被链接——检查 LINK_LIBRARY:WHOLE_ARCHIVE 配置。
    auto names = ProtocolFactory::instance().registeredNames();
    bool found = false;
    for (const auto& n : names) {
        if (n == "mock") { found = true; break; }
    }
    REQUIRE(found);
}

TEST_CASE("ProtocolFactory create mock returns non-null with correct name", "[protocol][factory]") {
    auto p = ProtocolFactory::instance().create("mock");
    REQUIRE(p != nullptr);
    REQUIRE(p->name() == "mock");
}

TEST_CASE("ProtocolFactory create unknown name returns nullptr", "[protocol][factory]") {
    auto p = ProtocolFactory::instance().create("no_such_protocol_xyz");
    REQUIRE(p == nullptr);
}

TEST_CASE("ProtocolFactory create mock full round-trip via IDeviceProtocol interface", "[protocol][factory]") {
    auto p = ProtocolFactory::instance().create("mock");
    REQUIRE(p != nullptr);
    REQUIRE(p->configure(ProtocolConfig{}));
    REQUIRE(p->open());
    std::array<Reading, 8> buf{};
    std::size_t n = p->poll(buf.data(), buf.size());
    REQUIRE(n > 0);
    p->close();
    REQUIRE_FALSE(p->isOpen());
}

TEST_CASE("ProtocolFactory create mock returns distinct instances", "[protocol][factory]") {
    auto a = ProtocolFactory::instance().create("mock");
    auto b = ProtocolFactory::instance().create("mock");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(a.get() != b.get()); // separate allocations, not shared / 独立分配,非共享
}

TEST_CASE("ProtocolFactory manual registerProtocol returns true and create works", "[protocol][factory]") {
    bool ok = ProtocolFactory::instance().registerProtocol(
        "dummy_manual_xyz", [] { return std::make_unique<DummyProtocol>(); });
    REQUIRE(ok);
    auto p = ProtocolFactory::instance().create("dummy_manual_xyz");
    REQUIRE(p != nullptr);
    REQUIRE(p->name() == "dummy");
}

TEST_CASE("ProtocolFactory duplicate registration returns false and first creator wins", "[protocol][factory]") {
    // "mock" is already registered by MockProtocol self-registration (whole-archive).
    // "mock" 已由 MockProtocol 自注册(whole-archive)。
    bool ok = ProtocolFactory::instance().registerProtocol(
        "mock", [] { return std::make_unique<DummyProtocol>(); });
    REQUIRE_FALSE(ok); // duplicate — first registration wins / 重名,首注册生效
    auto p = ProtocolFactory::instance().create("mock");
    REQUIRE(p != nullptr);
    REQUIRE(p->name() == "mock"); // original creator still in effect / 原始 creator 仍有效
}
