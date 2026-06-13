/// modbus_probe — CLI tool for real-device Modbus TCP verification.
/// modbus_probe —— 真机 Modbus TCP 验证 CLI 工具。
///
/// Usage: modbus_probe [--plugin <dll-path>] [host:port] [unit] [start] [qty] [scale] [offset] [timeout_ms] [count]
/// 用法 : modbus_probe [--plugin <dll路径>] [host:port] [unit] [start] [qty] [scale] [offset] [timeout_ms] [count]
///
/// Defaults: 127.0.0.1:502  1  0  4  1.0  0.0  1000  10
/// 默认值  : 127.0.0.1:502  1  0  4  1.0  0.0  1000  10
///
/// --plugin must be argv[1] when given; positional args follow as before.
/// --plugin 若给出必须为 argv[1];后续位置参数与之前相同。

#include <cstdio>
#include <cstdlib>
#include <string>

#include "indusscope/protocol/PluginLoader.h"
#include "indusscope/protocol/ProtocolConfig.h"
#include "indusscope/protocol/ProtocolFactory.h"
#include "indusscope/protocol/Reading.h"

using namespace indusscope::protocol;

int main(int argc, char* argv[]) {
    // --- Parse optional --plugin flag (must be argv[1] if present) ---
    // 解析可选的 --plugin 标志(若存在必须为 argv[1])
    std::string plugin_path;
    int positional_start = 1;  // index of first positional arg / 第一个位置参数的下标

    if (argc > 1 && std::string(argv[1]) == "--plugin") {
        if (argc < 3) {
            std::fprintf(stderr, "modbus_probe: --plugin requires a path argument\n");
            return 1;
        }
        plugin_path      = argv[2];
        positional_start = 3;
    }

    // --- Load plugin if requested ---
    // 若指定则加载插件
    if (!plugin_path.empty()) {
        PluginLoader loader;
        if (!loader.load(plugin_path)) {
            std::fprintf(stderr,
                "modbus_probe: failed to load plugin \"%s\" — check path and ABI version\n",
                plugin_path.c_str());
            return 1;
        }
    }

    // --- Parse positional args with defaults ---
    // 位置参数解析,带默认值
    auto arg = [&](int offset, const char* def) -> const char* {
        const int idx = positional_start + offset;
        return (idx < argc) ? argv[idx] : def;
    };

    const char* endpoint   = arg(0, "127.0.0.1:502");
    const char* unit       = arg(1, "1");
    const char* start_addr = arg(2, "0");
    const char* qty        = arg(3, "4");
    const char* scale      = arg(4, "1.0");
    const char* offset_str = arg(5, "0.0");
    const char* timeout_ms = arg(6, "1000");
    const int   count      = (positional_start + 7 < argc)
                                 ? std::atoi(argv[positional_start + 7]) : 10;

    // --- Create protocol via factory (requires prior PluginLoader::load for "modbus") ---
    // 通过工厂创建协议(modbus 需先调用 PluginLoader::load)
    auto proto = ProtocolFactory::instance().create("modbus");
    if (!proto) {
        std::fprintf(stderr,
            "modbus_probe: modbus protocol not available — load the plugin first with --plugin <path>\n");
        return 1;
    }

    ProtocolConfig cfg;
    cfg.endpoint                = endpoint;
    cfg.params["unit_id"]       = unit;
    cfg.params["start_address"] = start_addr;
    cfg.params["quantity"]      = qty;
    cfg.params["scale"]         = scale;
    cfg.params["offset"]        = offset_str;
    cfg.params["timeout_ms"]    = timeout_ms;

    proto->configure(cfg);

    std::fprintf(stdout,
        "modbus_probe: connecting to %s  unit=%s start=%s qty=%s "
        "scale=%s offset=%s timeout=%sms count=%d\n",
        endpoint, unit, start_addr, qty, scale, offset_str, timeout_ms, count);

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
