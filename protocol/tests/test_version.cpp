#include <catch2/catch_test_macros.hpp>
#include "indusscope/protocol/version.h"

TEST_CASE("protocol::version() returns a non-empty string", "[protocol][version]") {
    REQUIRE(!indusscope::protocol::version().empty());
}
