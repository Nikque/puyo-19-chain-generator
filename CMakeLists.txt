cmake_minimum_required(VERSION 3.16)
project(puyo_19_chain_generator VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)
if(MSVC)
    add_compile_options(/utf-8)
    # Static runtime: the distributed executables need no extra DLL.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    # core/field_bits.h uses SSE4.1; PEXT/PDEP is selected at run time.
    add_compile_options(-msse4.1 -mpopcnt)
endif()

# The engine: bitboard core (header only) and the generator.
add_library(puyo_engine STATIC
    generator/generator.cpp
    generator/runner.cpp
    generator/generator.h
    core/field_bits.h core/bit_field.h core/reach.h core/rng.h
    app/console_output.h)
find_package(Threads REQUIRED)
target_link_libraries(puyo_engine PUBLIC Threads::Threads)

add_executable(random_19_chain app/main.cpp)
target_link_libraries(random_19_chain PRIVATE puyo_engine)
if(MINGW)
    target_link_options(random_19_chain PRIVATE -municode)
endif()

if(WIN32)
    enable_language(RC)
    add_executable(puyo_gui WIN32 gui/puyo_gui.cpp gui/puyo_gui.rc)
    set_target_properties(puyo_gui PROPERTIES OUTPUT_NAME PuyoChainGenerator)
    target_link_libraries(puyo_gui PRIVATE puyo_engine comctl32 comdlg32 shell32 ole32 gdi32 user32)
    if(MSVC)
        target_link_options(puyo_gui PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/gui/puyo_gui.manifest")
    endif()
    if(MINGW)
        target_link_options(puyo_gui PRIVATE -municode -static -static-libgcc -static-libstdc++)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    # Bitboard core and generator against cell-by-cell references, once with
    # the SSE implementation and once with the portable 64-bit one.
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE puyo_engine)
    add_test(NAME core_and_generator COMMAND core_tests)
    add_executable(core_tests_portable tests/core_tests.cpp generator/generator.cpp generator/runner.cpp)
    target_compile_definitions(core_tests_portable PRIVATE PUYO_FORCE_PORTABLE)
    target_link_libraries(core_tests_portable PRIVATE Threads::Threads)
    add_test(NAME core_and_generator_portable COMMAND core_tests_portable)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(WIN32)
        foreach(name gui_model_tests editor_tests console_tests)
            add_executable(${name} tests/${name}.cpp)
            target_link_libraries(${name} PRIVATE puyo_engine)
            if(MINGW)
                target_link_options(${name} PRIVATE -municode)
            endif()
        endforeach()
        add_test(NAME gui_model_and_archives COMMAND gui_model_tests "${CMAKE_CURRENT_BINARY_DIR}/gui-model-test")
        add_test(NAME editor_model_and_specials COMMAND editor_tests "${CMAKE_CURRENT_BINARY_DIR}/editor-test")
        add_test(NAME gui_native_integration COMMAND puyo_gui --self-test "${CMAKE_CURRENT_BINARY_DIR}/gui-native-test")
        set_tests_properties(gui_native_integration PROPERTIES TIMEOUT 240)
    endif()
    if(Python3_Interpreter_FOUND)
        if(WIN32)
            add_test(NAME editor_independent_simulation
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_editor.py" "${CMAKE_CURRENT_BINARY_DIR}/editor-test")
            set_tests_properties(editor_independent_simulation PROPERTIES DEPENDS editor_model_and_specials)
            add_test(NAME gui_independent_replay
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gui_artifacts.py"
                    "${CMAKE_CURRENT_BINARY_DIR}/gui-native-test" "${CMAKE_CURRENT_BINARY_DIR}/gui-model-test")
            set_tests_properties(gui_independent_replay PROPERTIES DEPENDS "gui_native_integration;gui_model_and_archives")
            add_test(NAME windows_console_utf8
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_console.py"
                    "$<TARGET_FILE:console_tests>" "$<TARGET_FILE:random_19_chain>")
        endif()
        add_test(NAME independent_output_verification
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_output.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/solutions.log" --allow-extra)
        add_test(NAME config_and_generation_integration
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py"
                "$<TARGET_FILE:random_19_chain>")
        # Every chain length, colour count and extra range, verified independently.
        add_test(NAME all_chain_lengths
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_all_chains.py"
                "$<TARGET_FILE:random_19_chain>" "${CMAKE_CURRENT_SOURCE_DIR}/config.ini")
        set_tests_properties(all_chain_lengths PROPERTIES TIMEOUT 300)
    endif()
endif()

add_custom_target(generator_runtime_files ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"
        "$<TARGET_FILE_DIR:random_19_chain>/config.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/run.cmd"
        "$<TARGET_FILE_DIR:random_19_chain>/run.cmd"
    DEPENDS random_19_chain
)
