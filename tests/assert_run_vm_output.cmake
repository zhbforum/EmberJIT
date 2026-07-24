if(NOT DEFINED EXPECTED_OUTPUT_FILE)
    message(FATAL_ERROR "EXPECTED_OUTPUT_FILE must name a file with expected stdout.")
endif()

execute_process(
    COMMAND "${EMBER}" run --no-jit "${SOURCE_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Expected exit code 0, got '${result}': '${standard_error}'.")
endif()

if(NOT "${standard_error}" STREQUAL "")
    message(FATAL_ERROR "VM run wrote diagnostics: '${standard_error}'.")
endif()

file(READ "${EXPECTED_OUTPUT_FILE}" expected_output)

if(NOT "${standard_output}" STREQUAL "${expected_output}")
    message(FATAL_ERROR "Unexpected VM output: '${standard_output}'.")
endif()
