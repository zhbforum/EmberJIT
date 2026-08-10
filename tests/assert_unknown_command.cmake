execute_process(
    COMMAND "${EMBER}" unknown-command missing.ember
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "2")
    message(FATAL_ERROR "Expected exit code 2, got '${result}'.")
endif()

if(NOT "${standard_output}" STREQUAL "")
    message(FATAL_ERROR "Unknown command wrote stdout: '${standard_output}'.")
endif()

if(NOT standard_error MATCHES "^error: unknown command")
    message(FATAL_ERROR
        "Unknown command did not report its cause: '${standard_error}'."
    )
endif()

if(NOT standard_error MATCHES
   "usage: .*<command> \\[options\\] <file>")
    message(FATAL_ERROR "Unexpected usage output: '${standard_error}'.")
endif()

if(NOT standard_error MATCHES "benchmark")
    message(FATAL_ERROR "General usage omitted benchmark: '${standard_error}'.")
endif()
