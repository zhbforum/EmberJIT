execute_process(
    COMMAND "${EMBER}" dump-ast "${SOURCE_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "1")
    message(FATAL_ERROR "Expected exit code 1, got '${result}'.")
endif()

if(NOT "${standard_output}" STREQUAL "")
    message(FATAL_ERROR "dump-ast wrote partial output: '${standard_output}'.")
endif()

if(NOT standard_error MATCHES "invalid_ast\\.ember:1:29: error\\[E2002\\]")
    message(FATAL_ERROR "Unexpected diagnostic: '${standard_error}'.")
endif()
