add_library(indusscope_protocol STATIC
    src/version.cpp
    src/MockProtocol.cpp
    src/ProtocolFactory.cpp
)
add_library(IndusScope::protocol ALIAS indusscope_protocol)

target_include_directories(indusscope_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
