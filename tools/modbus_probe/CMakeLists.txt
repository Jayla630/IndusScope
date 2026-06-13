add_executable(modbus_probe
    main.cpp
)

# WHOLE_ARCHIVE forces static-init TUs (INDUSSCOPE_REGISTER_PROTOCOL) to be pulled in,
# enabling ProtocolFactory::create("modbus") to find its registration at runtime.
# WHOLE_ARCHIVE 强制静态初始化 TU (INDUSSCOPE_REGISTER_PROTOCOL) 被链接,
# 使 ProtocolFactory::create("modbus") 在运行时能找到注册项。
target_link_libraries(modbus_probe
    PRIVATE
        $<LINK_LIBRARY:WHOLE_ARCHIVE,IndusScope::protocol>
)

# ws2_32 is already transitively inherited via IndusScope::protocol PUBLIC ws2_32,
# but listed explicitly here to make the dependency visible at the tool level.
# ws2_32 已通过 IndusScope::protocol PUBLIC ws2_32 传递,
# 此处显式列出以在工具层使依赖一目了然。
if(WIN32)
    target_link_libraries(modbus_probe PRIVATE ws2_32)
endif()
