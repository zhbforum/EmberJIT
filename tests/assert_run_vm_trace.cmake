if(NOT DEFINED EXPECTED_OUTPUT_FILE OR NOT DEFINED EXPECTED_TRACE_FILE OR NOT DEFINED JIT_THRESHOLD)
    message(FATAL_ERROR "Expected output, trace, and threshold inputs must be defined.")
endif()

execute_process(
    COMMAND "${EMBER}" run --no-jit "--jit-threshold=${JIT_THRESHOLD}" --trace-jit "${SOURCE_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Expected exit code 0, got '${result}': '${standard_error}'.")
endif()

file(READ "${EXPECTED_OUTPUT_FILE}" expected_output)
if(NOT "${standard_output}" STREQUAL "${expected_output}")
    message(FATAL_ERROR "Unexpected VM output: '${standard_output}'.")
endif()

file(READ "${EXPECTED_TRACE_FILE}" expected_trace)
if(NOT "${standard_error}" STREQUAL "${expected_trace}")
    message(FATAL_ERROR "Unexpected JIT trace: '${standard_error}'.")
endif()
