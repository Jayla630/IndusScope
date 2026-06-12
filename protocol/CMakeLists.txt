add_library(indusscope_protocol STATIC
    src/version.cpp
    src/MockProtocol.cpp
    src/ModbusProtocol.cpp
    src/ProtocolFactory.cpp
    src/TcpTransport.cpp
)
add_library(IndusScope::protocol ALIAS indusscope_protocol)

target_include_directories(indusscope_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Winsock2 is needed by TcpTransport; PUBLIC so consumers (modbus_probe, app) inherit it.
# TcpTransport 依赖 Winsock2;PUBLIC 使下游(modbus_probe、app)自动继承。
if(WIN32)
    target_link_libraries(indusscope_protocol PUBLIC ws2_32)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
