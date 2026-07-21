execute_process(
    COMMAND "${EMBER}" dump-typed-ast "${SOURCE_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Expected exit code 0, got '${result}'.")
endif()

if(NOT "${standard_error}" STREQUAL "")
    message(FATAL_ERROR "dump-typed-ast wrote diagnostics: '${standard_error}'.")
endif()

if(NOT standard_output MATCHES "TypedProgram" OR NOT standard_output MATCHES "Binary \\+: i64")
    message(FATAL_ERROR "Typed AST dump does not contain typed binary expression: '${standard_output}'.")
endif()
