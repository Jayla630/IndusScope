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

# WHOLE_ARCHIVE future-proofs registration here; in THIS binary ModbusProtocol.o is
# pulled in anyway because tests reference the class directly (injection constructor).
# The load-bearing dead-strip sentinel lives in test_protocol_factory, which never
# references concrete protocol types.
# WHOLE_ARCHIVE 在此仅作保险;本二进制的测试直接引用类(注入构造),
# ModbusProtocol.o 本就会被链入。真正受力的防裁剪哨兵在 test_protocol_factory——
# 它从不直接引用具体协议类型。
target_link_libraries(test_protocol_modbus
    PRIVATE
        $<LINK_LIBRARY:WHOLE_ARCHIVE,IndusScope::protocol>
        Catch2::Catch2WithMain
)

catch_discover_tests(test_protocol_modbus)
