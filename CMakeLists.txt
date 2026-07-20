cmake_minimum_required(VERSION 3.25)

project(
    EmberJIT
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ember
    src/main.cpp
)

if(MSVC)
    target_compile_options(ember PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(ember PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
endif()

include(CTest)

if(BUILD_TESTING)
    add_test(
        NAME ember_smoke
        COMMAND ember
    )

    set_tests_properties(ember_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "EmberJIT 0\\.1\\.0"
    )
endif()
