#include <catch2/catch_test_macros.hpp>
#include "indusscope/core/version.h"

TEST_CASE("core::version() returns a non-empty string", "[core][version]") {
    REQUIRE(!indusscope::core::version().empty());
}
