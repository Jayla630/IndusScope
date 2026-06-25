add_library(protocol_modbus SHARED
    ModbusPlugin.cpp
)

# modbus_core supplies ModbusProtocol + TcpTransport implementations.
# modbus_core 提供 ModbusProtocol + TcpTransport 实现。
# No WHOLE_ARCHIVE — must not pull ProtocolFactory / MockProtocol into the DLL.
# 不使用 WHOLE_ARCHIVE——不得将 ProtocolFactory / MockProtocol 拉进 DLL。
target_link_libraries(protocol_modbus
    PRIVATE modbus_core
)

# On Windows, PLUGIN_EXPORT = __declspec(dllexport) handles symbol visibility.
# Windows 上,PLUGIN_EXPORT = __declspec(dllexport) 处理符号可见性。
# On POSIX, default ELF visibility is sufficient unless -fvisibility=hidden is set.
# POSIX 上,默认 ELF 可见性已足够,除非设置了 -fvisibility=hidden。

# Co-locate DLL with IndusScope.exe so applicationDirPath() lookup works without PATH tricks.
# 将 DLL 与 IndusScope.exe 放在同一目录,使 applicationDirPath() 查找无需 PATH 技巧。
set_target_properties(protocol_modbus PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
