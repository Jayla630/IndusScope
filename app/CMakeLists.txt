add_executable(IndusScope
    main.cpp
)

target_link_libraries(IndusScope
    PRIVATE
        IndusScope::ui
        indusscope_uiplugin
        IndusScope::core
        # WHOLE_ARCHIVE forces static-init TUs (INDUSSCOPE_REGISTER_PROTOCOL) to be pulled in,
        # enabling ProtocolFactory::create("mock") to find its registration at runtime.
        # WHOLE_ARCHIVE 强制静态初始化 TU (INDUSSCOPE_REGISTER_PROTOCOL) 被链接,
        # 使 ProtocolFactory::create("mock") 在运行时能找到注册项。
        $<LINK_LIBRARY:WHOLE_ARCHIVE,IndusScope::protocol>
        Qt6::Quick
        Qt6::Qml
        Qt6::Graphs
)

# Ensure protocol_modbus DLL is built before IndusScope.exe (runtime dlopen dep).
# 确保 protocol_modbus DLL 在 IndusScope.exe 之前构建 (运行时 dlopen 依赖)。
# IndusScope does NOT link protocol_modbus at build time — dlopen at runtime only.
# IndusScope 不在构建时链接 protocol_modbus——仅在运行时 dlopen。
add_dependencies(IndusScope protocol_modbus)
