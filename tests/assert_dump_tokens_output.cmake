execute_process(
    COMMAND "${EMBER}" dump-tokens "${SOURCE_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Expected exit code 0, got '${result}'.")
endif()

if(NOT "${standard_error}" STREQUAL "")
    message(FATAL_ERROR
        "dump-tokens wrote diagnostics: '${standard_error}'."
    )
endif()

set(expected_output [=[fn [0, 2)
identifier [3, 4)
-> [5, 7)
integer_literal [8, 10)
end_of_file [11, 11)
]=])

if(NOT "${standard_output}" STREQUAL "${expected_output}")
    message(FATAL_ERROR
        "Unexpected token dump: '${standard_output}'."
    )
endif()
