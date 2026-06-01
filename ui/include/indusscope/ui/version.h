#pragma once

#include <string>

namespace indusscope::ui {

/// Return the ui layer version string.
/// Header is pure C++; implementation links Qt6.
std::string version();

} // namespace indusscope::ui
