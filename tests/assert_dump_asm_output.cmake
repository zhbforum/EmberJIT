execute_process(
    COMMAND "${EMBER}" dump-asm "${SOURCE_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Expected exit code 0, got '${result}'.")
endif()

if(NOT "${standard_error}" STREQUAL "")
    message(FATAL_ERROR "dump-asm wrote diagnostics: '${standard_error}'.")
endif()

if(NOT standard_output MATCHES "fn #[0-9]+:" OR
   NOT standard_output MATCHES "0000: [0-9A-F][0-9A-F]")
    message(FATAL_ERROR "Assembly dump is missing a compiled function: '${standard_output}'.")
endif()
