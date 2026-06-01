add_executable(IndusScope
    main.cpp
)

target_link_libraries(IndusScope
    PRIVATE
        IndusScope::ui
        IndusScope::core
        IndusScope::protocol
)
