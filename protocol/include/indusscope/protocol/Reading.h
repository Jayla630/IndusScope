#pragma once

#include <cstdint>
#include <type_traits>

namespace indusscope::protocol {

/// One acquired data point from a device channel.
/// 设备通道的一个采集数据点。
struct Reading {
    std::uint32_t channel;      // logical channel index / 逻辑通道号
    double        value;        // converted engineering value / 转换后的工程量(协议内部负责量纲转换)
    std::int64_t  timestamp_ns; // acquisition time, ns / 采集时刻(纳秒)
};

static_assert(std::is_trivially_copyable_v<Reading>,
              "Reading must be trivially copyable for ring-buffer value semantics / "
              "Reading 必须可平凡拷贝,环形缓冲按值搬运");

} // namespace indusscope::protocol
