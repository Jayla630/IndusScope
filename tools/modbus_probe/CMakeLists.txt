add_executable(modbus_probe
    main.cpp
)

# WHOLE_ARCHIVE pulls in MockProtocol.o (self-registration) and PluginLoader.o.
# WHOLE_ARCHIVE 拉入 MockProtocol.o(自注册)和 PluginLoader.o。
# modbus is no longer statically built-in; it is loaded at runtime via PluginLoader.
# modbus 已不再静态内建;由 PluginLoader 在运行时加载。
target_link_libraries(modbus_probe
    PRIVATE
        $<LINK_LIBRARY:WHOLE_ARCHIVE,IndusScope::protocol>
)

# ws2_32 not needed here: TcpTransport lives inside protocol_modbus.dll (loaded dynamically);
# PluginLoader uses LoadLibrary which is in Kernel32 (auto-linked).
# ws2_32 此处不需要:TcpTransport 在 protocol_modbus.dll 中(动态加载);
# PluginLoader 使用 LoadLibrary,位于自动链接的 Kernel32 中。
