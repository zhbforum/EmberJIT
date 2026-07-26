execute_process(
    COMMAND "${EMBER}" unknown-command missing.ember
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "2")
    message(FATAL_ERROR "Expected exit code 2, got '${result}'.")
endif()

if(NOT "${standard_output}" STREQUAL "")
    message(FATAL_ERROR "Unknown command wrote stdout: '${standard_output}'.")
endif()

string(CONCAT dump_usage_pattern
    "usage: .*<dump-tokens\\|dump-ast\\|dump-typed-ast\\|dump-bytecode"
    "\\|dump-ir> <file>"
)
if(NOT standard_error MATCHES "${dump_usage_pattern}")
    message(FATAL_ERROR "Unexpected usage output: '${standard_error}'.")
endif()

string(CONCAT run_usage_pattern
    "run \\[--no-jit\\] \\[--jit-threshold=<non-negative-integer>\\] "
    "\\[--trace-jit\\] <file>"
)
if(NOT standard_error MATCHES "${run_usage_pattern}")
    message(FATAL_ERROR "Unexpected usage output: '${standard_error}'.")
endif()
