add_executable(test_core_version
    test_version.cpp
)

target_link_libraries(test_core_version
    PRIVATE
        IndusScope::core
        Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(test_core_version)
