#pragma once

#include <string>

namespace indusscope::core {

/// Return the core layer version string.
/// Core is a pure C++ engine — zero Qt dependency (SPEC Section 4).
std::string version();

} // namespace indusscope::core
