execute_process(
    COMMAND "${EMBER}" run --no-jit "${SOURCE_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
        "Expected exit code 0, got '${result}': '${standard_error}'."
    )
endif()

if(NOT "${standard_output}" STREQUAL "")
    message(FATAL_ERROR "VM run wrote stdout: '${standard_output}'.")
endif()

if(NOT "${standard_error}" STREQUAL "")
    message(FATAL_ERROR "VM run wrote diagnostics: '${standard_error}'.")
endif()
