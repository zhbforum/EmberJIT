execute_process(
    COMMAND "${EMBER}" dump-tokens "${SOURCE_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "1")
    message(FATAL_ERROR "Expected exit code 1, got '${result}'.")
endif()

if(NOT "${standard_output}" STREQUAL "")
    message(FATAL_ERROR
        "dump-tokens wrote partial output: '${standard_output}'."
    )
endif()

if(NOT standard_error MATCHES "invalid_token\\.ember:1:3: error\\[E1003\\]")
    message(FATAL_ERROR "Unexpected diagnostic: '${standard_error}'.")
endif()
