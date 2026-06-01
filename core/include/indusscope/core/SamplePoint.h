#pragma once

#include <cstdint>
#include <type_traits>

namespace indusscope::core {

struct SamplePoint {
    /// Sampling timestamp (steady_clock nanoseconds).
    /// 采样时刻 (steady_clock 纳秒)。
    std::int64_t timestamp_ns;

    /// Sampled value.
    /// 采样值。
    double value;
};

static_assert(std::is_trivially_copyable_v<SamplePoint>,
              "SamplePoint must be trivially copyable for ring-buffer value semantics / "
              "SamplePoint 必须可平凡拷贝,环形缓冲按值搬运,后续可零拷贝/memcpy");

} // namespace indusscope::core
