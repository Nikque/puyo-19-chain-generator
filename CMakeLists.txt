cmake_minimum_required(VERSION 3.16)
project(puyo_19_chain_generator VERSION 0.3.0 LANGUAGES CXX)

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
if(MINGW)
    target_link_options(random_19_chain PRIVATE -municode)
endif()

if(WIN32)
    enable_language(RC)
    add_executable(puyo_gui WIN32 puyo_gui.cpp puyo_gui.rc)
    set_target_properties(puyo_gui PROPERTIES
        OUTPUT_NAME PuyoChainGenerator
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    target_link_libraries(puyo_gui PRIVATE comctl32 comdlg32 shell32 ole32 gdi32 user32)
    if(MSVC)
        target_compile_options(puyo_gui PRIVATE /utf-8)
        set_property(TARGET puyo_gui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_link_options(puyo_gui PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/puyo_gui.manifest")
    endif()
    if(MINGW)
        target_link_options(puyo_gui PRIVATE -municode -static -static-libgcc -static-libstdc++)
    endif()
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
    if(WIN32)
        add_executable(gui_model_tests tests/gui_model_tests.cpp)
        set_target_properties(gui_model_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)
        if(MSVC)
            target_compile_options(gui_model_tests PRIVATE /utf-8)
            set_property(TARGET gui_model_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
        if(MINGW)
            target_link_options(gui_model_tests PRIVATE -municode)
        endif()
        add_test(NAME gui_model_and_archives COMMAND gui_model_tests "${CMAKE_CURRENT_BINARY_DIR}/gui-model-test")
        add_executable(editor_tests tests/editor_tests.cpp)
        set_target_properties(editor_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)
        if(MSVC)
            target_compile_options(editor_tests PRIVATE /utf-8)
            set_property(TARGET editor_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
        if(MINGW)
            target_link_options(editor_tests PRIVATE -municode)
        endif()
        add_test(NAME editor_model_and_specials COMMAND editor_tests "${CMAKE_CURRENT_BINARY_DIR}/editor-test")
        add_test(NAME gui_native_integration COMMAND puyo_gui --self-test "${CMAKE_CURRENT_BINARY_DIR}/gui-native-test")
        set_tests_properties(gui_native_integration PROPERTIES TIMEOUT 240)
        add_executable(console_tests tests/console_tests.cpp)
        set_target_properties(console_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)
        if(MSVC)
            target_compile_options(console_tests PRIVATE /utf-8)
            set_property(TARGET console_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
        if(MINGW)
            target_link_options(console_tests PRIVATE -municode)
        endif()
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
