#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "FakeTransport.h"
#include "indusscope/protocol/ModbusProtocol.h"
#include "indusscope/protocol/ProtocolFactory.h"

using indusscope::protocol::FakeTransport;
using indusscope::protocol::ModbusProtocol;
using indusscope::protocol::ProtocolConfig;
using indusscope::protocol::ProtocolFactory;
using indusscope::protocol::Reading;

namespace {

// --- Test helpers 测试辅助 ---

// Build a well-formed FC03 response ADU for the given transaction id and register values.
// 按给定事务号与寄存器值构造合法的 FC03 响应 ADU。
std::vector<std::uint8_t> makeResponse(std::uint16_t tid,
                                       const std::vector<std::uint16_t>& regs,
                                       std::uint8_t unit_id = 0x11) {
    std::vector<std::uint8_t> r;
    const auto len = static_cast<std::uint16_t>(3 + 2 * regs.size()); // UnitID+FC+BC+2N
    r.push_back(static_cast<std::uint8_t>(tid >> 8));
    r.push_back(static_cast<std::uint8_t>(tid & 0xFF));
    r.push_back(0x00); r.push_back(0x00);                  // Protocol ID = 0 / 协议号恒 0
    r.push_back(static_cast<std::uint8_t>(len >> 8));
    r.push_back(static_cast<std::uint8_t>(len & 0xFF));
    r.push_back(unit_id);                                  // Unit ID / 单元号
    r.push_back(0x03);                                     // Function / 功能码
    r.push_back(static_cast<std::uint8_t>(2 * regs.size())); // Byte count / 字节数
    for (const auto v : regs) {
        r.push_back(static_cast<std::uint8_t>(v >> 8));
        r.push_back(static_cast<std::uint8_t>(v & 0xFF));
    }
    return r;
}

// Holder pairing an injected protocol with a raw observer pointer to its FakeTransport.
// 将注入后的协议与其 FakeTransport 的观察指针配对持有。
struct Rig {
    std::unique_ptr<ModbusProtocol> proto;
    FakeTransport* fake; // owned by proto / 所有权在 proto
};

// Build a configured ModbusProtocol over a FakeTransport (classic spec example:
// unit 17, start 107, quantity 3, plus caller-supplied extra params).
// 在 FakeTransport 上构造已配置的 ModbusProtocol(经典规范示例:
// 单元 17、起始 107、数量 3,再叠加调用方追加的参数)。
Rig makeRig(std::map<std::string, std::string> extra = {}) {
    auto t = std::make_unique<FakeTransport>();
    auto* fake = t.get();
    auto p = std::make_unique<ModbusProtocol>(std::move(t));

    ProtocolConfig cfg;
    cfg.endpoint = "192.168.1.10:502";
    cfg.params = {{"unit_id", "17"}, {"start_address", "107"}, {"quantity", "3"}};
    for (auto& kv : extra) cfg.params[kv.first] = kv.second;

    REQUIRE(p->configure(cfg));
    return Rig{std::move(p), fake};
}

// The exact 12-byte FC03 request the rig above must emit for transaction id `tid`.
// 上述 rig 在事务号 tid 下必须发出的 12 字节 FC03 请求帧。
std::vector<std::uint8_t> expectedRequest(std::uint16_t tid) {
    return {
        static_cast<std::uint8_t>(tid >> 8),
        static_cast<std::uint8_t>(tid & 0xFF), // Transaction ID / 事务号
        0x00, 0x00,                            // Protocol ID / 协议号
        0x00, 0x06,                            // Length = 6 / 长度
        0x11,                                  // Unit ID = 17 / 单元号
        0x03,                                  // Function / 功能码
        0x00, 0x6B,                            // Starting address = 107 / 起始地址
        0x00, 0x03,                            // Quantity = 3 / 寄存器数
    };
}

} // anonymous namespace

// --- Request encoding 请求编码 ---

TEST_CASE("modbus encodes exact FC03 request frame", "[modbus]") {
    auto rig = makeRig();
    REQUIRE(rig.proto->open());

    rig.fake->enqueueChunk(makeResponse(1, {0, 0, 0}));
    Reading out[8];
    REQUIRE(rig.proto->poll(out, 8) == 3);

    // Byte-for-byte: BE transaction/protocol/length, unit id, FC, BE address & quantity.
    // 逐字节比对:大端事务号/协议号/长度、单元号、功能码、大端地址与数量。
    CHECK(rig.fake->sent() == expectedRequest(1));
}

TEST_CASE("modbus transaction id increments per poll", "[modbus]") {
    auto rig = makeRig();
    REQUIRE(rig.proto->open());
    Reading out[4];

    rig.fake->enqueueChunk(makeResponse(1, {1, 2, 3}));
    REQUIRE(rig.proto->poll(out, 4) == 3);
    CHECK(rig.fake->sent() == expectedRequest(1));

    rig.fake->clearSent();
    rig.fake->enqueueChunk(makeResponse(2, {4, 5, 6}));
    REQUIRE(rig.proto->poll(out, 4) == 3);
    CHECK(rig.fake->sent() == expectedRequest(2));
}

// --- Normal decode 正常解码 ---

TEST_CASE("modbus decodes registers big-endian with default scaling", "[modbus]") {
    auto rig = makeRig();
    REQUIRE(rig.proto->open());

    // 0x0102 = 258, 0x0000 = 0, 0xFFFF = 65535 — BE join must use (b0<<8)|b1.
    // 0x0102=258、0x0000=0、0xFFFF=65535——大端拼装必须按 (b0<<8)|b1。
    rig.fake->enqueueChunk(makeResponse(1, {0x0102, 0x0000, 0xFFFF}));
    Reading out[8];
    REQUIRE(rig.proto->poll(out, 8) == 3);

    CHECK(out[0].channel == 0);
    CHECK(out[1].channel == 1);
    CHECK(out[2].channel == 2);
    CHECK(out[0].value == 258.0);
    CHECK(out[1].value == 0.0);
    CHECK(out[2].value == 65535.0);

    // Timestamp invariant: one steady_clock stamp per scan, shared by all N readings.
    // 时间戳不变量:每次扫描取一次 steady_clock,N 个读数共用。
    CHECK(out[0].timestamp_ns == out[1].timestamp_ns);
    CHECK(out[1].timestamp_ns == out[2].timestamp_ns);

    // Across polls timestamps are non-decreasing (steady_clock is monotonic; equal is legal).
    // 跨 poll 时间戳非递减(steady_clock 单调;相等合法)。
    const auto ts_first = out[0].timestamp_ns;
    rig.fake->enqueueChunk(makeResponse(2, {1, 2, 3}));
    REQUIRE(rig.proto->poll(out, 8) == 3);
    CHECK(out[0].timestamp_ns >= ts_first);
}

TEST_CASE("modbus applies scale and offset to raw values", "[modbus]") {
    auto rig = makeRig({{"scale", "0.1"}, {"offset", "-10.0"}});
    REQUIRE(rig.proto->open());

    rig.fake->enqueueChunk(makeResponse(1, {0x0000, 0x0101, 0xFFFF}));
    Reading out[4];
    REQUIRE(rig.proto->poll(out, 4) == 3);

    // Expected values computed with the same expression shape: raw * scale + offset.
    // 期望值用同形表达式计算:raw * scale + offset。
    CHECK(out[0].value == 0.0 * 0.1 + (-10.0));
    CHECK(out[1].value == 257.0 * 0.1 + (-10.0));
    CHECK(out[2].value == 65535.0 * 0.1 + (-10.0));
}

TEST_CASE("modbus truncates to max_n but keeps stream in sync", "[modbus]") {
    auto rig = makeRig();
    REQUIRE(rig.proto->open());
    Reading out[4];

    // max_n = 2 < quantity = 3: two readings out, full response still consumed.
    // max_n=2 < quantity=3:写出 2 个读数,但响应字节全量读完。
    rig.fake->enqueueChunk(makeResponse(1, {10, 20, 30}));
    REQUIRE(rig.proto->poll(out, 2) == 2);
    CHECK(out[0].value == 10.0);
    CHECK(out[1].value == 20.0);

    // Next transaction still decodes — proves no stale bytes were left in the stream.
    // 下一次事务仍正常解码——证明流里没有残留脏字节。
    rig.fake->enqueueChunk(makeResponse(2, {40, 50, 60}));
    REQUIRE(rig.proto->poll(out, 4) == 3);
    CHECK(out[2].value == 60.0);

    // max_n = 0 returns 0 without touching the wire.
    // max_n=0 直接返 0,不碰线上。
    rig.fake->clearSent();
    CHECK(rig.proto->poll(out, 0) == 0);
    CHECK(rig.fake->sent().empty());
}

// --- Partial & truncated reads 部分读取与截断 ---

TEST_CASE("modbus reassembles response from partial recv chunks", "[modbus]") {
    auto rig = makeRig();
    REQUIRE(rig.proto->open());

    // Full 15-byte response split as 4+5+6: fragments cross the MBAP/PDU boundary,
    // forcing recvExact to loop both within one call and across calls.
    // 完整 15 字节响应切成 4+5+6:分片跨越 MBAP/PDU 边界,
    // 逼出 recvExact 在单次调用内与跨调用的循环拼装。
    const auto resp = makeResponse(1, {0x0102, 0x0304, 0x0506});
    REQUIRE(resp.size() == 15);
    rig.fake->enqueueChunk({resp.begin(), resp.begin() + 4});
    rig.fake->enqueueChunk({resp.begin() + 4, resp.begin() + 9});
    rig.fake->enqueueChunk({resp.begin() + 9, resp.end()});

    Reading out[4];
    REQUIRE(rig.proto->poll(out, 4) == 3);
    CHECK(out[0].value == static_cast<double>(0x0102));
    CHECK(out[1].value == static_cast<double>(0x0304));
    CHECK(out[2].value == static_cast<double>(0x0506));
}

TEST_CASE("modbus returns zero when response is cut off mid frame", "[modbus]") {
    Reading out[4];

    SECTION("stream dies inside MBAP header / MBAP 头读一半就断") {
        auto rig = makeRig();
        REQUIRE(rig.proto->open());
        const auto resp = makeResponse(1, {1, 2, 3});
        rig.fake->enqueueChunk({resp.begin(), resp.begin() + 4}); // then recv -> 0 / 之后 recv 返 0
        CHECK(rig.proto->poll(out, 4) == 0);
    }

    SECTION("stream dies inside PDU body / PDU 体读一半就断") {
        auto rig = makeRig();
        REQUIRE(rig.proto->open());
        const auto resp = makeResponse(1, {1, 2, 3});
        rig.fake->enqueueChunk({resp.begin(), resp.begin() + 11}); // MBAP + partial PDU / MBAP+半截 PDU
        CHECK(rig.proto->poll(out, 4) == 0);
    }

    SECTION("nothing arrives at all / 整帧无响应(超时)") {
        auto rig = makeRig();
        REQUIRE(rig.proto->open());
        CHECK(rig.proto->poll(out, 4) == 0);
    }
}

// --- Validation rejections 校验拒绝 ---

TEST_CASE("modbus rejects exception response without parsing", "[modbus]") {
    auto rig = makeRig();
    REQUIRE(rig.proto->open());

    // Exception ADU: Length = 3 (UnitID + 0x83 + exception code 0x02 illegal address).
    // 异常 ADU:长度 = 3(单元号 + 0x83 + 异常码 0x02 非法地址)。
    rig.fake->enqueueChunk({0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x11, 0x83, 0x02});
    Reading out[4];
    CHECK(rig.proto->poll(out, 4) == 0);
}

TEST_CASE("modbus rejects transaction id mismatch", "[modbus]") {
    auto rig = makeRig();
    REQUIRE(rig.proto->open());

    rig.fake->enqueueChunk(makeResponse(0x99, {1, 2, 3})); // we sent tid=1 / 实发事务号为 1
    Reading out[4];
    CHECK(rig.proto->poll(out, 4) == 0);
}

TEST_CASE("modbus rejects nonzero protocol id", "[modbus]") {
    auto rig = makeRig();
    REQUIRE(rig.proto->open());

    auto resp = makeResponse(1, {1, 2, 3});
    resp[2] = 0xDE; resp[3] = 0xAD; // corrupt protocol id / 篡改协议号
    rig.fake->enqueueChunk(std::move(resp));
    Reading out[4];
    CHECK(rig.proto->poll(out, 4) == 0);
}

TEST_CASE("modbus rejects hostile length field without overflow", "[modbus]") {
    Reading out[4];

    // Hostile server claims absurd Length: must be rejected BEFORE any recvExact
    // into the fixed PDU buffer — no crash, no out-of-bounds write, just 0.
    // 敌意服务器谎报长度:必须在向定长 PDU 缓冲 recvExact 之前拒绝——
    // 不崩、不越界写,直接返 0。
    SECTION("length 0xFFFF / 长度灌大") {
        auto rig = makeRig();
        REQUIRE(rig.proto->open());
        rig.fake->enqueueChunk({0x00, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0x11});
        CHECK(rig.proto->poll(out, 4) == 0);
    }

    SECTION("length 0 below minimum / 长度 0 低于下限") {
        auto rig = makeRig();
        REQUIRE(rig.proto->open());
        rig.fake->enqueueChunk({0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x11});
        CHECK(rig.proto->poll(out, 4) == 0);
    }

    SECTION("length 2 below minimum / 长度 2 低于下限") {
        auto rig = makeRig();
        REQUIRE(rig.proto->open());
        rig.fake->enqueueChunk({0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x11, 0x03});
        CHECK(rig.proto->poll(out, 4) == 0);
    }
}

TEST_CASE("modbus rejects byte count inconsistencies", "[modbus]") {
    Reading out[4];

    SECTION("byte count does not match requested quantity / 字节数与请求数量不符") {
        auto rig = makeRig();
        REQUIRE(rig.proto->open());
        rig.fake->enqueueChunk(makeResponse(1, {1, 2})); // 2 regs while quantity=3 / 只回 2 个寄存器
        CHECK(rig.proto->poll(out, 4) == 0);
    }

    SECTION("byte count overruns received frame / 字节数越过实收帧") {
        auto rig = makeRig();
        REQUIRE(rig.proto->open());
        // Length claims PDU = FC + BC only (L=3), but byte count claims 6 data bytes:
        // decode must not read past the received range into stale buffer memory.
        // 长度声明 PDU 仅含功能码+字节数(L=3),但 byte count 谎称 6 字节数据:
        // 解码不得越过实收范围读缓冲脏数据。
        rig.fake->enqueueChunk({0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x11, 0x03, 0x06});
        CHECK(rig.proto->poll(out, 4) == 0);
    }
}

// --- Lifecycle & configuration 生命周期与配置 ---

TEST_CASE("modbus open close lifecycle delegates to transport", "[modbus]") {
    Reading out[4];

    SECTION("default-constructed has no transport: open fails, poll returns 0 / 默认构造无 transport") {
        ModbusProtocol p;
        ProtocolConfig cfg;
        cfg.endpoint = "10.0.0.1:502";
        CHECK(p.configure(cfg));   // configure still parses normally / configure 仍正常解析
        CHECK_FALSE(p.open());
        CHECK_FALSE(p.isOpen());
        CHECK(p.poll(out, 4) == 0);
        p.close();                 // close on transportless instance must not crash / 无 transport 时 close 不得崩
    }

    SECTION("injected transport: open/isOpen/close delegate / 注入后三法委托 transport") {
        auto rig = makeRig();
        CHECK_FALSE(rig.proto->isOpen());
        CHECK(rig.proto->poll(out, 4) == 0); // poll before open / 未 open 先 poll
        REQUIRE(rig.proto->open());
        CHECK(rig.proto->isOpen());
        rig.proto->close();
        CHECK_FALSE(rig.proto->isOpen());
        CHECK(rig.proto->poll(out, 4) == 0); // poll after close / close 后 poll
    }

    SECTION("send failure returns 0 / 发送失败返 0") {
        auto rig = makeRig();
        REQUIRE(rig.proto->open());
        rig.fake->setFailSend(true);
        CHECK(rig.proto->poll(out, 4) == 0);
    }
}

TEST_CASE("modbus configure defaults and clamping", "[modbus]") {
    SECTION("defaults: quantity 1 -> channelCount 1 / 默认 quantity=1") {
        ModbusProtocol p;
        ProtocolConfig cfg;
        cfg.endpoint = "10.0.0.1:502";
        REQUIRE(p.configure(cfg));
        CHECK(p.channelCount() == 1);
    }

    SECTION("quantity above FC03 limit clamps to 125 / 超上限夹到 125") {
        ModbusProtocol p;
        ProtocolConfig cfg;
        cfg.params = {{"quantity", "300"}};
        REQUIRE(p.configure(cfg));
        CHECK(p.channelCount() == 125);
    }

    SECTION("quantity zero clamps to 1 / 零夹到 1") {
        ModbusProtocol p;
        ProtocolConfig cfg;
        cfg.params = {{"quantity", "0"}};
        REQUIRE(p.configure(cfg));
        CHECK(p.channelCount() == 1);
    }

    SECTION("garbage params keep defaults / 垃圾参数保留默认") {
        ModbusProtocol p;
        ProtocolConfig cfg;
        cfg.params = {{"quantity", "abc"}, {"scale", "xyz"}, {"unit_id", "999"}};
        REQUIRE(p.configure(cfg));
        CHECK(p.channelCount() == 1);
    }

    SECTION("default unit id and start address appear in frame / 默认单元号与起始地址进帧") {
        auto t = std::make_unique<FakeTransport>();
        auto* fake = t.get();
        ModbusProtocol p{std::move(t)};
        ProtocolConfig cfg;
        cfg.endpoint = "10.0.0.1:502"; // no params: unit_id=1, start=0, quantity=1 / 无参数全默认
        REQUIRE(p.configure(cfg));
        REQUIRE(p.open());
        fake->enqueueChunk(makeResponse(1, {7}, /*unit_id=*/0x01));
        Reading out[2];
        REQUIRE(p.poll(out, 2) == 1);
        const std::vector<std::uint8_t> expected = {
            0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
        CHECK(fake->sent() == expected);
        CHECK(out[0].value == 7.0);
    }
}

// --- Factory behavior 工厂行为 ---
// NOTE: this binary references ModbusProtocol directly (injection tests), which pulls
// ModbusProtocol.o into the link regardless of WHOLE_ARCHIVE — so this case can NOT
// detect dead-stripping. The true sentinel lives in test_protocol_factory.cpp, which
// never includes concrete protocol headers.
// 注意:本二进制直接引用 ModbusProtocol(注入测试),无论是否 WHOLE_ARCHIVE,
// ModbusProtocol.o 都会被链入——故本用例【无法】侦测 dead-strip。
// 真正的哨兵在 test_protocol_factory.cpp,它从不 include 具体协议头。

TEST_CASE("modbus factory create and registeredNames", "[modbus][factory]") {
    auto& factory = ProtocolFactory::instance();

    auto names = factory.registeredNames();
    bool found = false;
    for (const auto& n : names) {
        if (n == "modbus") { found = true; break; }
    }
    CHECK(found);

    auto p = factory.create("modbus");
    REQUIRE(p != nullptr);
    CHECK(p->name() == "modbus");
    CHECK_FALSE(p->isOpen()); // factory instance has no transport yet / 工厂实例尚无 transport
    CHECK_FALSE(p->open());   // open fails until S2.5c TcpTransport / S2.5c 接入前 open 失败
}
