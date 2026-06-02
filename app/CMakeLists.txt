add_executable(IndusScope
    main.cpp
)

target_link_libraries(IndusScope
    PRIVATE
        IndusScope::ui
        indusscope_uiplugin
        IndusScope::core
        IndusScope::protocol
        Qt6::Quick
        Qt6::Qml
        Qt6::Graphs
)
