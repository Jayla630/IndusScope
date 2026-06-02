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

add_executable(test_core_ring_buffer
    test_ring_buffer.cpp
)

target_link_libraries(test_core_ring_buffer
    PRIVATE
        IndusScope::core
        Catch2::Catch2WithMain
)

catch_discover_tests(test_core_ring_buffer)

add_executable(test_core_signal_generator
    test_signal_generator.cpp
)

target_link_libraries(test_core_signal_generator
    PRIVATE
        IndusScope::core
        Catch2::Catch2WithMain
)

catch_discover_tests(test_core_signal_generator)
