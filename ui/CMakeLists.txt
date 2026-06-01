add_library(indusscope_ui STATIC
    src/version.cpp
)
add_library(IndusScope::ui ALIAS indusscope_ui)

target_include_directories(indusscope_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(indusscope_ui
    PUBLIC
        IndusScope::core
        Qt6::Core
        Qt6::Quick
)
