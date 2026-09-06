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

option(
    EMBER_ENABLE_MSVC_ANALYZE
    "Enable MSVC static code analysis"
    OFF
)

function(ember_configure_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /EHsc
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

    if(EMBER_ENABLE_MSVC_ANALYZE)
        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            message(FATAL_ERROR
                "EMBER_ENABLE_MSVC_ANALYZE requires the Microsoft C++ compiler"
            )
        endif()

        target_compile_options(${target} PRIVATE /analyze)
    endif()
endfunction()

add_library(ember_core INTERFACE)

target_include_directories(ember_core
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(ember_support
    src/support/source.cpp
)

target_include_directories(ember_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

ember_configure_compile_options(ember_support)

add_library(ember_frontend
    src/frontend/ast_printer.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
)

target_link_libraries(ember_frontend PUBLIC ember_support)
ember_configure_compile_options(ember_frontend)

add_library(ember_semantic
    src/semantic/analyzer.cpp
    src/semantic/typed_ast_printer.cpp
)
target_link_libraries(ember_semantic PUBLIC ember_core ember_frontend)
ember_configure_compile_options(ember_semantic)

add_library(ember_bytecode
    src/bytecode/bytecode.cpp
)
target_link_libraries(ember_bytecode PUBLIC ember_core ember_support)
ember_configure_compile_options(ember_bytecode)

add_library(ember_bytecode_lowering
    src/bytecode/lowering.cpp
)
target_link_libraries(ember_bytecode_lowering
    PUBLIC
        ember_bytecode
    PRIVATE
        ember_semantic
)
ember_configure_compile_options(ember_bytecode_lowering)

add_library(ember_builtin_integration
    src/integration/builtin_registration.cpp
)
target_link_libraries(ember_builtin_integration
    PRIVATE
        ember_bytecode
        ember_semantic
)
target_include_directories(ember_builtin_integration
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
ember_configure_compile_options(ember_builtin_integration)

add_library(ember_ir
    src/ir/ir.cpp
    src/ir/verifier.cpp
    src/ir/dump.cpp
    src/ir/optimization.cpp
)
target_link_libraries(ember_ir PUBLIC ember_core ember_support)
ember_configure_compile_options(ember_ir)

add_library(ember_ir_lowering
    src/ir/bytecode_lowerer.cpp
)
target_link_libraries(ember_ir_lowering PUBLIC ember_ir ember_bytecode)
ember_configure_compile_options(ember_ir_lowering)

add_library(ember_ssa
    src/ssa/ssa.cpp
    src/ssa/verifier.cpp
    src/ssa/dump.cpp
)
target_link_libraries(ember_ssa PUBLIC ember_core ember_support)
ember_configure_compile_options(ember_ssa)

add_library(ember_jit
    src/jit/baseline_compiler.cpp
    src/jit/code_buffer.cpp
    src/jit/emitter.cpp
)
target_link_libraries(ember_jit
    PUBLIC
        ember_core
        ember_support
        ember_ir
    PRIVATE
        ember_bytecode
)
ember_configure_compile_options(ember_jit)

add_library(ember_runtime
    src/runtime/runtime_dispatcher.cpp
    src/runtime/runtime_function.cpp
    src/runtime/runtime_function_table.cpp
    src/runtime/native_code.cpp
    src/runtime/native_value.cpp
    src/runtime/vm.cpp
)
target_link_libraries(ember_runtime
    PUBLIC
        ember_core
        ember_bytecode
        ember_jit
    PRIVATE
        ember_ir_lowering
)
ember_configure_compile_options(ember_runtime)

add_executable(ember
    src/main.cpp
)

target_link_libraries(ember
    PRIVATE
        ember_runtime
        ember_semantic
        ember_ir_lowering
        ember_bytecode_lowering
        ember_builtin_integration
)
ember_configure_compile_options(ember)

option(EMBER_BUILD_BENCHMARKS "Build Ember runtime benchmarks" OFF)

if(EMBER_BUILD_BENCHMARKS)
    add_executable(ember_runtime_benchmark
        benchmarks/runtime_dispatch_benchmark.cpp
    )
    target_link_libraries(ember_runtime_benchmark PRIVATE ember_runtime)
    ember_configure_compile_options(ember_runtime_benchmark)
endif()

include(CTest)

if(BUILD_TESTING)
    set(ember_example_directory "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    set(ember_fixture_directory "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    set(ember_test_script_directory "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    set(
        ember_architecture_directory
        "${ember_test_script_directory}/architecture"
    )
    set(
        ember_architecture_test_script
        "${ember_architecture_directory}/assert_header_dependencies.cmake"
    )
    set(
        ember_deterministic_output_script
        "${ember_test_script_directory}/assert_deterministic_output.cmake"
    )
    set(
        ember_large_input_script
        "${ember_test_script_directory}/assert_large_input.cmake"
    )
    set(
        ember_generated_test_directory
        "${CMAKE_CURRENT_BINARY_DIR}/generated-tests"
    )

    set(
        ember_invalid_token_fixture
        "${ember_fixture_directory}/invalid_token.ember"
    )

    set(
        ember_dump_tokens_fixture
        "${ember_fixture_directory}/dump_tokens.ember"
    )

    set(
        ember_dump_ast_fixture
        "${ember_fixture_directory}/dump_ast.ember"
    )

    set(
        ember_dump_ir_fixture
        "${ember_fixture_directory}/dump_ir.ember"
    )

    set(
        ember_determinism_fixture
        "${ember_fixture_directory}/determinism.ember"
    )

    set(
        ember_determinism_invalid_fixture
        "${ember_fixture_directory}/determinism_invalid.ember"
    )

    set(
        ember_golden_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
    )

    set(
        ember_invalid_ast_fixture
        "${ember_fixture_directory}/invalid_ast.ember"
    )

    set(
        ember_invalid_typed_ast_fixture
        "${ember_fixture_directory}/invalid_typed_ast.ember"
    )

    set(
        ember_vm_runtime_fixture
        "${ember_fixture_directory}/vm_runtime.ember"
    )

    set(
        ember_native_v01_types_fixture
        "${ember_fixture_directory}/native_v01_types.ember"
    )

    set(
        ember_invalid_main_signature_fixture
        "${ember_fixture_directory}/invalid_main_signature.ember"
    )

    set(
        ember_invalid_main_return_fixture
        "${ember_fixture_directory}/invalid_main_return.ember"
    )

    set(
        ember_missing_main_fixture
        "${ember_fixture_directory}/missing_main.ember"
    )

    set(
        ember_main_i64_success_fixture
        "${ember_fixture_directory}/main_i64_success.ember"
    )

    set(
        ember_vm_runtime_expected_output
        "${ember_fixture_directory}/vm_runtime.expected"
    )

    set(
        ember_native_v01_types_expected_output
        "${ember_fixture_directory}/native_v01_types.expected"
    )

    set(
        ember_vm_factorial_expected_output
        "${ember_fixture_directory}/vm_factorial.expected"
    )

    set(
        ember_vm_fibonacci_expected_output
        "${ember_fixture_directory}/vm_fibonacci.expected"
    )

    set(
        ember_vm_hot_loop_expected_output
        "${ember_fixture_directory}/vm_hot_loop.expected"
    )

    set(
        ember_vm_hot_loop_expected_trace
        "${ember_fixture_directory}/vm_hot_loop.trace.expected"
    )

    add_executable(ember_tests
        tests/test_main.cpp
        tests/support/source_tests.cpp
        tests/frontend/token_tests.cpp
        tests/frontend/lexer_tests.cpp
        tests/frontend/parser_tests.cpp
        tests/semantic/analyzer_tests.cpp
        tests/bytecode/bytecode_tests.cpp
        tests/ir/ir_tests.cpp
        tests/ssa/ssa_tests.cpp
        tests/jit/baseline_compiler_tests.cpp
        tests/jit/emitter_tests.cpp
        tests/runtime/runtime_tests.cpp
    )

    target_link_libraries(ember_tests
        PRIVATE
            ember_runtime
            ember_semantic
            ember_ir_lowering
            ember_ssa
            ember_bytecode_lowering
            ember_builtin_integration
    )
    target_include_directories(ember_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    ember_configure_compile_options(ember_tests)

    add_library(ember_core_headers_compile OBJECT
        tests/architecture/core_type_header_compile.cpp
        tests/architecture/core_value_header_compile.cpp
        tests/architecture/core_function_header_compile.cpp
    )
    target_link_libraries(ember_core_headers_compile PRIVATE ember_core)
    ember_configure_compile_options(ember_core_headers_compile)

    add_library(ember_bytecode_lowering_header_compile OBJECT
        tests/architecture/bytecode_lowering_header_compile.cpp
    )
    target_link_libraries(ember_bytecode_lowering_header_compile
        PRIVATE ember_bytecode_lowering
    )
    ember_configure_compile_options(ember_bytecode_lowering_header_compile)

    add_library(ember_builtin_integration_header_compile OBJECT
        tests/architecture/builtin_registration_header_compile.cpp
    )
    target_link_libraries(ember_builtin_integration_header_compile
        PRIVATE ember_builtin_integration
    )
    ember_configure_compile_options(ember_builtin_integration_header_compile)

    add_library(ember_native_value_header_compile OBJECT
        tests/architecture/native_value_header_compile.cpp
    )
    target_link_libraries(ember_native_value_header_compile PRIVATE ember_core)
    ember_configure_compile_options(ember_native_value_header_compile)

    # Each consumer links only its own library; the main test executable can
    # otherwise hide missing dependencies by linking sibling libraries.
    foreach(stage IN ITEMS bytecode ir ssa jit)
        set(ember_link_test_target "ember_architecture_${stage}_link")
        add_executable(${ember_link_test_target}
            tests/test_main.cpp
            tests/architecture/${stage}_link_tests.cpp
        )
        target_link_libraries(${ember_link_test_target} PRIVATE ember_${stage})
        target_include_directories(${ember_link_test_target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
        )
        ember_configure_compile_options(${ember_link_test_target})
        add_test(
        NAME ${ember_link_test_target}
        COMMAND ${ember_link_test_target}
        )
    endforeach()

    add_test(
        NAME ember_smoke
        COMMAND ember
    )

    add_test(
        NAME ember_tests
        COMMAND ember_tests
    )

    add_test(
        NAME ember_architecture_header_dependencies
        COMMAND ${CMAKE_COMMAND}
            "-DEMBER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P "${ember_architecture_test_script}"
    )

    add_test(
        NAME ember_dump_tokens
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_dump_tokens_fixture}
            -P ${ember_test_script_directory}/assert_dump_tokens_output.cmake
    )

    add_test(
        NAME ember_dump_tokens_invalid
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_invalid_token_fixture}
            -P ${ember_test_script_directory}/assert_dump_tokens_error.cmake
    )

    add_test(
        NAME ember_dump_ast
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_dump_ast_fixture}
            -P ${ember_test_script_directory}/assert_dump_ast_output.cmake
    )

    add_test(
        NAME ember_dump_ast_invalid
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_invalid_ast_fixture}
            -P ${ember_test_script_directory}/assert_dump_ast_error.cmake
    )

    add_test(
        NAME ember_dump_typed_ast
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_dump_ast_fixture}
            -P ${ember_test_script_directory}/assert_dump_typed_ast_output.cmake
    )

    add_test(
        NAME ember_dump_typed_ast_invalid
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_invalid_typed_ast_fixture}
            -P ${ember_test_script_directory}/assert_dump_typed_ast_error.cmake
    )

    add_test(
        NAME ember_dump_ir
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_dump_ir_fixture}
            -P ${ember_test_script_directory}/assert_dump_ir_output.cmake
    )

    add_test(
        NAME ember_dump_asm
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_dump_ir_fixture}
            -P ${ember_test_script_directory}/assert_dump_asm_output.cmake
    )

    foreach(dump_command IN ITEMS tokens ast typed-ast bytecode ir asm)
        string(REPLACE "-" "_" golden_name "${dump_command}")
        add_test(
            NAME ember_deterministic_dump_${golden_name}
            COMMAND ${CMAKE_COMMAND}
                -DEMBER=$<TARGET_FILE:ember>
                -DMODE=golden
                -DCOMMAND_NAME=dump-${dump_command}
                -DSOURCE_FILE=${ember_determinism_fixture}
                -DGOLDEN_FILE=${ember_golden_directory}/dump_${golden_name}.txt
                -P ${ember_deterministic_output_script}
        )
    endforeach()

    add_test(
        NAME ember_deterministic_invalid_dump
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DMODE=repeated-invalid
            -DCOMMAND_NAME=dump-asm
            -DSOURCE_FILE=${ember_determinism_invalid_fixture}
            -DEXPECTED_EXIT_CODE=1
            -P ${ember_deterministic_output_script}
    )

    add_test(
        NAME ember_large_valid_input
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DMODE=large-valid
            -DTEST_TEMP_DIR=${ember_generated_test_directory}/large-valid
            -P ${ember_large_input_script}
    )

    add_test(
        NAME ember_large_malformed_input
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DMODE=large-malformed
            -DTEST_TEMP_DIR=${ember_generated_test_directory}/large-malformed
            -P ${ember_large_input_script}
    )

    add_test(
        NAME ember_hostile_path_input
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DMODE=hostile-path
            -DTEST_TEMP_DIR=${ember_generated_test_directory}/hostile-path
            -P ${ember_large_input_script}
    )

    add_test(
        NAME ember_unknown_command
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -P ${ember_test_script_directory}/assert_unknown_command.cmake
    )

    add_test(
        NAME ember_cli_contract
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -P ${ember_test_script_directory}/assert_cli_contract.cmake
    )

    add_test(
        NAME ember_cli_usage_errors
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_vm_runtime_fixture}
            -P ${ember_test_script_directory}/assert_cli_usage_errors.cmake
    )

    add_test(
        NAME ember_cli_missing_source
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -P ${ember_test_script_directory}/assert_cli_missing_source.cmake
    )

    if(WIN32)
        add_test(
            NAME ember_benchmark
            COMMAND ${CMAKE_COMMAND}
                -DEMBER=$<TARGET_FILE:ember>
                -DSOURCE_FILE=${ember_example_directory}/vm_hot_loop.ember
                -P ${ember_test_script_directory}/assert_benchmark_output.cmake
        )
    endif()

    add_test(
        NAME ember_run_vm
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_vm_runtime_fixture}
            -DEXPECTED_OUTPUT_FILE=${ember_vm_runtime_expected_output}
            -P ${ember_test_script_directory}/assert_run_vm_output.cmake
    )

    add_test(
        NAME ember_run_jit_native_v01_types
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_native_v01_types_fixture}
            -DEXPECTED_OUTPUT_FILE=${ember_native_v01_types_expected_output}
            -DJIT_THRESHOLD=1
            -P ${ember_test_script_directory}/assert_run_jit_output.cmake
    )

    add_test(
        NAME ember_run_vm_invalid_main_signature
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_invalid_main_signature_fixture}
            -DEXPECTED_ERROR_PATTERN=must.not.accept.parameters
            -P ${ember_test_script_directory}/assert_run_vm_error.cmake
    )

    add_test(
        NAME ember_run_vm_invalid_main_return
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_invalid_main_return_fixture}
            -DEXPECTED_ERROR_PATTERN=must.return.void.or.i64
            -P ${ember_test_script_directory}/assert_run_vm_error.cmake
    )

    add_test(
        NAME ember_run_vm_missing_main
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_missing_main_fixture}
            -DEXPECTED_ERROR_PATTERN=missing.user.entry.function
            -P ${ember_test_script_directory}/assert_run_vm_error.cmake
    )

    add_test(
        NAME ember_run_vm_main_i64_success
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_main_i64_success_fixture}
            -P ${ember_test_script_directory}/assert_run_vm_success.cmake
    )

    add_test(
        NAME ember_run_vm_factorial
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_example_directory}/vm_factorial.ember
            -DEXPECTED_OUTPUT_FILE=${ember_vm_factorial_expected_output}
            -P ${ember_test_script_directory}/assert_run_vm_output.cmake
    )

    add_test(
        NAME ember_run_vm_fibonacci
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_example_directory}/vm_fibonacci.ember
            -DEXPECTED_OUTPUT_FILE=${ember_vm_fibonacci_expected_output}
            -P ${ember_test_script_directory}/assert_run_vm_output.cmake
    )

    add_test(
        NAME ember_run_vm_hot_loop
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_example_directory}/vm_hot_loop.ember
            -DEXPECTED_OUTPUT_FILE=${ember_vm_hot_loop_expected_output}
            -P ${ember_test_script_directory}/assert_run_vm_output.cmake
    )

    add_test(
        NAME ember_run_vm_hot_loop_trace
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_example_directory}/vm_hot_loop.ember
            -DEXPECTED_OUTPUT_FILE=${ember_vm_hot_loop_expected_output}
            -DEXPECTED_TRACE_FILE=${ember_vm_hot_loop_expected_trace}
            -DJIT_THRESHOLD=3
            -P ${ember_test_script_directory}/assert_run_vm_trace.cmake
    )

    add_test(
        NAME ember_run_vm_threshold_zero
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_example_directory}/vm_hot_loop.ember
            -DEXPECTED_OUTPUT_FILE=${ember_vm_hot_loop_expected_output}
            -P ${ember_test_script_directory}/assert_run_vm_threshold_zero.cmake
    )

    add_test(
        NAME ember_run_vm_invalid_options
        COMMAND ${CMAKE_COMMAND}
            -DEMBER=$<TARGET_FILE:ember>
            -DSOURCE_FILE=${ember_example_directory}/vm_hot_loop.ember
            -P ${ember_test_script_directory}/assert_run_option_errors.cmake
    )

    set_tests_properties(ember_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "usage: .*<command> \\[options\\] <file>"
    )

endif()
