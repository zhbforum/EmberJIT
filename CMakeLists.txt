cmake_minimum_required(VERSION 3.25)

project(
    EmberJIT
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(ember_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
        /W4
        /permissive-
        )
    else()
        target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        )
    endif()
endfunction()

add_library(ember_support
    src/support/source.cpp
)

target_include_directories(ember_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

ember_enable_warnings(ember_support)

add_library(ember_frontend
    src/frontend/lexer.cpp
)

target_link_libraries(ember_frontend PUBLIC ember_support)
ember_enable_warnings(ember_frontend)

add_executable(ember
    src/main.cpp
)

target_link_libraries(ember PRIVATE ember_frontend)
ember_enable_warnings(ember)

include(CTest)

if(BUILD_TESTING)
    set(
        ember_invalid_token_fixture
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/invalid_token.ember"
    )

    set(
        ember_dump_tokens_fixture
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/dump_tokens.ember"
    )

    add_executable(ember_tests
        tests/test_main.cpp
        tests/support/source_tests.cpp
        tests/frontend/token_tests.cpp
        tests/frontend/lexer_tests.cpp
    )

    target_link_libraries(ember_tests PRIVATE ember_frontend)
    target_include_directories(ember_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    ember_enable_warnings(ember_tests)

    add_test(
        NAME ember_smoke
        COMMAND ember
    )

    add_test(
        NAME ember_tests
        COMMAND ember_tests
    )

    add_test(
        NAME ember_dump_tokens
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_dump_tokens_fixture}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_dump_tokens_output.cmake
    )

    add_test(
        NAME ember_dump_tokens_invalid
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_invalid_token_fixture}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_dump_tokens_error.cmake
    )

    set_tests_properties(ember_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "EmberJIT 0\\.1\\.0"
    )

endif()
