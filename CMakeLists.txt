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

add_executable(ember
    src/main.cpp
)

target_link_libraries(ember PRIVATE ember_support)
ember_enable_warnings(ember)

include(CTest)

if(BUILD_TESTING)
    add_executable(ember_tests
        tests/support/source_tests.cpp
    )

    target_link_libraries(ember_tests PRIVATE ember_support)
    ember_enable_warnings(ember_tests)

    add_test(
        NAME ember_smoke
        COMMAND ember
    )

    add_test(
        NAME ember_tests
        COMMAND ember_tests
    )

    set_tests_properties(ember_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "EmberJIT 0\\.1\\.0"
    )
endif()
