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

add_executable(test_protocol_mock
    test_mock_protocol.cpp
)

target_link_libraries(test_protocol_mock
    PRIVATE
        IndusScope::protocol
        Catch2::Catch2WithMain
)

catch_discover_tests(test_protocol_mock)

add_executable(test_protocol_factory
    test_protocol_factory.cpp
)

target_link_libraries(test_protocol_factory
    PRIVATE
        $<LINK_LIBRARY:WHOLE_ARCHIVE,IndusScope::protocol>
        Catch2::Catch2WithMain
)

catch_discover_tests(test_protocol_factory)

add_executable(test_protocol_modbus
    test_modbus_protocol.cpp
)

# WHOLE_ARCHIVE keeps ModbusProtocol.o alive so its self-registration runs —
# the factory-sentinel test case guards against silent dead-stripping.
# WHOLE_ARCHIVE 保住 ModbusProtocol.o,使其自注册得以执行——
# 工厂哨兵测试用例防止其被静默裁剪。
target_link_libraries(test_protocol_modbus
    PRIVATE
        $<LINK_LIBRARY:WHOLE_ARCHIVE,IndusScope::protocol>
        Catch2::Catch2WithMain
)

catch_discover_tests(test_protocol_modbus)
