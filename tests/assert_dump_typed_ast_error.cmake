execute_process(
    COMMAND "${EMBER}" dump-typed-ast "${SOURCE_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "1")
    message(FATAL_ERROR "Expected exit code 1, got '${result}'.")
endif()

if(NOT "${standard_output}" STREQUAL "")
    message(FATAL_ERROR "dump-typed-ast wrote stdout: '${standard_output}'.")
endif()

if(NOT standard_error MATCHES "error\\[E3006\\]")
    message(FATAL_ERROR "Expected semantic diagnostic E3006: '${standard_error}'.")
endif()
