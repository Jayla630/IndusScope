/// modbus_probe — CLI tool for real-device Modbus TCP verification (S2.5c).
/// modbus_probe —— 真机 Modbus TCP 验证 CLI 工具(S2.5c)。
///
/// Usage: modbus_probe [host:port] [unit] [start] [qty] [scale] [offset] [timeout_ms] [count]
/// 用法 : modbus_probe [host:port] [unit] [start] [qty] [scale] [offset] [timeout_ms] [count]
///
/// Defaults: 127.0.0.1:502  1  0  4  1.0  0.0  1000  10
/// 默认值  : 127.0.0.1:502  1  0  4  1.0  0.0  1000  10

#include <cstdio>
#include <cstdlib>
#include <string>

#include "indusscope/protocol/ProtocolConfig.h"
#include "indusscope/protocol/ProtocolFactory.h"
#include "indusscope/protocol/Reading.h"

using namespace indusscope::protocol;

int main(int argc, char* argv[]) {
    // --- Parse argv with positional defaults ---
    // 位置参数解析,带默认值
    const char* endpoint   = (argc > 1) ? argv[1] : "127.0.0.1:502";
    const char* unit       = (argc > 2) ? argv[2] : "1";
    const char* start_addr = (argc > 3) ? argv[3] : "0";
    const char* qty        = (argc > 4) ? argv[4] : "4";
    const char* scale      = (argc > 5) ? argv[5] : "1.0";
    const char* offset     = (argc > 6) ? argv[6] : "0.0";
    const char* timeout_ms = (argc > 7) ? argv[7] : "1000";
    const int   count      = (argc > 8) ? std::atoi(argv[8]) : 10;

    ProtocolConfig cfg;
    cfg.endpoint                    = endpoint;
    cfg.params["unit_id"]           = unit;
    cfg.params["start_address"]     = start_addr;
    cfg.params["quantity"]          = qty;
    cfg.params["scale"]             = scale;
    cfg.params["offset"]            = offset;
    cfg.params["timeout_ms"]        = timeout_ms;

    // --- Create protocol via factory (WHOLE_ARCHIVE ensures registration is pulled in) ---
    // 通过工厂创建协议(WHOLE_ARCHIVE 确保注册 TU 被链入)
    auto proto = ProtocolFactory::instance().create("modbus");
    if (!proto) {
        std::fprintf(stderr,
            "modbus_probe: ProtocolFactory::create(\"modbus\") returned null\n"
            "  hint: is the protocol library linked with WHOLE_ARCHIVE?\n");
        return 1;
    }

    proto->configure(cfg);

    std::fprintf(stdout,
        "modbus_probe: connecting to %s  unit=%s start=%s qty=%s "
        "scale=%s offset=%s timeout=%sms count=%d\n",
        endpoint, unit, start_addr, qty, scale, offset, timeout_ms, count);

    if (!proto->open()) {
        std::fprintf(stderr,
            "modbus_probe: open() failed — check endpoint and Modbus Slave status\n");
        return 1;
    }

    std::fprintf(stdout, "modbus_probe: connected — polling %d rounds\n", count);
    std::fflush(stdout);

    constexpr std::size_t kMaxCh = 125;
    Reading readings[kMaxCh];

    for (int i = 0; i < count; ++i) {
        const std::size_t n = proto->poll(readings, kMaxCh);
        if (n == 0) {
            std::fprintf(stdout, "[poll #%d] no data (timeout or frame error)\n", i + 1);
        } else {
            std::fprintf(stdout, "[poll #%d]", i + 1);
            for (std::size_t c = 0; c < n; ++c)
                std::fprintf(stdout, "  ch%u=%.3f", readings[c].channel, readings[c].value);
            std::fprintf(stdout, "\n");
        }
        std::fflush(stdout);
    }

    proto->close();
    return 0;
}
