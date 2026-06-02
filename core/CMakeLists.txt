add_library(indusscope_core STATIC
    src/version.cpp
    src/SignalGenerator.cpp
    src/MockSource.cpp
)
add_library(IndusScope::core ALIAS indusscope_core)

target_include_directories(indusscope_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Core depends on protocol (dependency direction: core -> protocol)
target_link_libraries(indusscope_core
    PUBLIC IndusScope::protocol
)

# Zero Qt — SPEC Section 4 iron law
# No find_package(Qt6), no target_link_libraries(... Qt6::...)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
