cmake_minimum_required(VERSION 3.16)
project(puyo_19_chain_generator LANGUAGES CXX)

add_executable(random_19_chain random_19_chain.cpp)
set_target_properties(random_19_chain PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

if(MSVC)
    target_compile_options(random_19_chain PRIVATE /utf-8)
    set_property(TARGET random_19_chain PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_custom_command(TARGET random_19_chain POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"
        "$<TARGET_FILE_DIR:random_19_chain>/config.ini"
)
