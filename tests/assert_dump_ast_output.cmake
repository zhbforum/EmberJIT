execute_process(
    COMMAND "${EMBER}" dump-ast "${SOURCE_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Expected exit code 0, got '${result}'.")
endif()

if(NOT "${standard_error}" STREQUAL "")
    message(FATAL_ERROR "dump-ast wrote diagnostics: '${standard_error}'.")
endif()

set(expected_output [=[Program [0, 71)
  Function main -> i64 [0, 70)
    Block [17, 70)
      Let value: i64 [23, 50)
        Binary + [40, 49)
          Literal 1 [40, 41)
          Binary * [44, 49)
            Literal 2 [44, 45)
            Literal 3 [48, 49)
      Return [55, 68)
        Identifier value [62, 67)
]=])

if(NOT "${standard_output}" STREQUAL "${expected_output}")
    message(FATAL_ERROR "Unexpected AST dump: '${standard_output}'.")
endif()
