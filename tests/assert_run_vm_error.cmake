if(NOT DEFINED EXPECTED_ERROR_PATTERN)
    message(FATAL_ERROR
        "EXPECTED_ERROR_PATTERN must describe the entry-point error."
    )
endif()

execute_process(
    COMMAND "${EMBER}" run --no-jit "${SOURCE_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "1")
    message(FATAL_ERROR "Expected exit code 1, got '${result}'.")
endif()

if(NOT "${standard_output}" STREQUAL "")
    message(FATAL_ERROR
        "Invalid entry point wrote stdout: '${standard_output}'."
    )
endif()

if(NOT standard_error MATCHES "error\\[E5001\\]")
    message(FATAL_ERROR
        "Expected entry-point diagnostic E5001: '${standard_error}'."
    )
endif()

if(NOT standard_error MATCHES "${EXPECTED_ERROR_PATTERN}")
    message(FATAL_ERROR
        "Unexpected entry-point diagnostic: '${standard_error}'."
    )
endif()
