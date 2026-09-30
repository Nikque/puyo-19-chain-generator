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

include(CTest)
if(BUILD_TESTING)
    add_executable(generator_tests tests/tests.cpp)
    set_target_properties(generator_tests PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    if(MSVC)
        target_compile_options(generator_tests PRIVATE /utf-8)
        set_property(TARGET generator_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_test(NAME generator_regressions COMMAND generator_tests)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME independent_output_verification
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_output.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/solutions.log" --allow-extra)
        add_test(NAME config_and_generation_integration
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py"
                "$<TARGET_FILE:random_19_chain>")
        add_test(NAME all_chain_lengths
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_all_chains.py"
                "$<TARGET_FILE:random_19_chain>" "${CMAKE_CURRENT_SOURCE_DIR}/config.ini")
        set_tests_properties(all_chain_lengths PROPERTIES TIMEOUT 300)
    endif()
endif()

add_custom_command(TARGET random_19_chain POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"
        "$<TARGET_FILE_DIR:random_19_chain>/config.ini"
)
