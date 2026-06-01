add_executable(test_protocol_version
    test_version.cpp
)

target_link_libraries(test_protocol_version
    PRIVATE
        IndusScope::protocol
        Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(test_protocol_version)
