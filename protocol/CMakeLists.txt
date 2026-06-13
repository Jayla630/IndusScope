# --- Protocol API + factory + mock + plugin loader (no Modbus/TCP socket code) ---
# 协议 API + 工厂 + mock + 插件加载器(不含 Modbus/TCP socket 代码)
add_library(indusscope_protocol STATIC
    src/version.cpp
    src/MockProtocol.cpp
    src/ProtocolFactory.cpp
    src/PluginLoader.cpp
)
add_library(IndusScope::protocol ALIAS indusscope_protocol)

target_include_directories(indusscope_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# PluginLoader uses dlopen on POSIX. CMAKE_DL_LIBS is empty on glibc 2.34+ (already in libc).
# PluginLoader 在 POSIX 上使用 dlopen。CMAKE_DL_LIBS 在 glibc 2.34+ 为空(已内置于 libc)。
# PUBLIC: propagates to consumers that pull in PluginLoader.o via whole-archive.
# PUBLIC:向通过 whole-archive 拉入 PluginLoader.o 的消费者传递此依赖。
if(UNIX)
    target_link_libraries(indusscope_protocol PUBLIC ${CMAKE_DL_LIBS})
endif()

# --- modbus_core: Modbus framing + TcpTransport (static; not in the protocol static lib) ---
# modbus_core:Modbus 帧逻辑 + TcpTransport(静态;不在 protocol 静态库中)
# Linked by: protocol_modbus (SHARED) + test_protocol_modbus; NOT by indusscope_protocol.
# 链接方:protocol_modbus (SHARED) + test_protocol_modbus;不由 indusscope_protocol 链接。
add_library(modbus_core STATIC
    src/ModbusProtocol.cpp
    src/TcpTransport.cpp
)

# PUBLIC: exposes protocol's include paths to dependents (test_protocol_modbus, protocol_modbus).
# PUBLIC:向下游(test_protocol_modbus、protocol_modbus)传递 protocol 头文件路径。
# Not WHOLE_ARCHIVE — mock self-registration must NOT enter modbus_core or the DLL.
# 非 WHOLE_ARCHIVE——mock 自注册不得进入 modbus_core 或 DLL。
target_link_libraries(modbus_core
    PUBLIC IndusScope::protocol
)

# Winsock2 is needed by TcpTransport; PUBLIC so consumers (test_protocol_modbus) inherit it.
# TcpTransport 依赖 Winsock2;PUBLIC 使下游(test_protocol_modbus)自动继承。
if(WIN32)
    target_link_libraries(modbus_core PUBLIC ws2_32)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# --- protocol_modbus: the runtime-loaded DLL/.so ---
# protocol_modbus:运行时加载的 DLL/.so
add_subdirectory(plugins/modbus)
