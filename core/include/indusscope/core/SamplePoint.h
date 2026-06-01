#pragma once

#include <cstdint>
#include <type_traits>

namespace indusscope::core {

struct SamplePoint {
    std::int64_t timestamp_ns;  // 采样时刻(steady_clock 纳秒)
    double       value;         // 采样值
};

static_assert(std::is_trivially_copyable_v<SamplePoint>,
              "SamplePoint must be trivially copyable: "
              "ring buffer transports by value, future zero-copy/memcpy safe");

} // namespace indusscope::core
