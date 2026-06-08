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
