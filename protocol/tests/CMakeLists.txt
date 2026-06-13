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

# Links modbus_core (STATIC) directly — hermetic FakeTransport tests need ModbusProtocol
# and TcpTransport but must NOT depend on the protocol_modbus .dll.
# 直接链接 modbus_core(STATIC)——hermetic FakeTransport 测试需要 ModbusProtocol 和
# TcpTransport,但不得依赖 protocol_modbus .dll。
# ws2_32 propagates transitively via modbus_core PUBLIC.
# ws2_32 通过 modbus_core PUBLIC 传递继承。
target_link_libraries(test_protocol_modbus
    PRIVATE
        modbus_core
        Catch2::Catch2WithMain
)

catch_discover_tests(test_protocol_modbus)
