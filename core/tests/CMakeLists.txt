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

add_executable(test_core_mock_source
    test_mock_source.cpp
)

target_link_libraries(test_core_mock_source
    PRIVATE
        IndusScope::core
        Catch2::Catch2WithMain
)

catch_discover_tests(test_core_mock_source)

add_executable(test_core_minmax_downsampler
    test_minmax_downsampler.cpp
)

target_link_libraries(test_core_minmax_downsampler
    PRIVATE
        IndusScope::core
        Catch2::Catch2WithMain
)

catch_discover_tests(test_core_minmax_downsampler)

add_executable(test_core_protocol_source
    test_protocol_source.cpp
)

target_link_libraries(test_core_protocol_source
    PRIVATE
        IndusScope::core
        Catch2::Catch2WithMain
)

catch_discover_tests(test_core_protocol_source)

add_executable(test_core_frame_pool
    test_frame_pool.cpp
)

target_link_libraries(test_core_frame_pool
    PRIVATE
        IndusScope::core
        Catch2::Catch2WithMain
)

catch_discover_tests(test_core_frame_pool)

add_executable(test_core_synthetic_frame_source
    test_synthetic_frame_source.cpp
)

target_link_libraries(test_core_synthetic_frame_source
    PRIVATE
        IndusScope::core
        Catch2::Catch2WithMain
)

catch_discover_tests(test_core_synthetic_frame_source)

add_executable(test_core_latest_frame_buffer
    test_core_latest_frame_buffer.cpp
)

target_link_libraries(test_core_latest_frame_buffer
    PRIVATE
        IndusScope::core
        Catch2::Catch2WithMain
)

catch_discover_tests(test_core_latest_frame_buffer)
