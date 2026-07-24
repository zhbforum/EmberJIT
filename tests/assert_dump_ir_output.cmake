execute_process(
    COMMAND "${EMBER}" dump-ir "${SOURCE_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Expected exit code 0, got '${result}'.")
endif()

if(NOT "${standard_error}" STREQUAL "")
    message(FATAL_ERROR "dump-ir wrote diagnostics: '${standard_error}'.")
endif()

if(NOT standard_output MATCHES "fn #[0-9]+ \\(\\) -> i64" OR
   NOT standard_output MATCHES "block b0:" OR
   NOT standard_output MATCHES "block b1:" OR
   NOT standard_output MATCHES "gt.i64" OR
   NOT standard_output MATCHES "branch_if_false" OR
   NOT standard_output MATCHES "branch b1" OR
   NOT standard_output MATCHES "return v")
    message(FATAL_ERROR "IR dump does not show the expected virtual-register function: '${standard_output}'.")
endif()
