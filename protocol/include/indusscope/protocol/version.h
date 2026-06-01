#pragma once

#include <string>

namespace indusscope::protocol {

/// Return the protocol layer version string.
/// Pure C++ — does not depend on Qt or any other layer.
std::string version();

} // namespace indusscope::protocol
