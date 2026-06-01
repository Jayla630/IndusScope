#pragma once

#include <string>

namespace indusscope::core {

/// Return the core layer version string.
/// 返回 core 层版本号字符串。
///
/// Core is a pure C++ engine — zero Qt dependency (SPEC Section 4).
/// Core 层是纯 C++ 引擎——零 Qt 依赖 (SPEC §4 铁律)。
std::string version();

} // namespace indusscope::core
