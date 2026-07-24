function(assert_usage_error)
    execute_process(
        COMMAND "${EMBER}" run ${ARGN} "${SOURCE_FILE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )

    if(NOT "${result}" STREQUAL "2")
        message(FATAL_ERROR "Expected exit code 2, got '${result}': '${standard_error}'.")
    endif()
    if(NOT "${standard_output}" STREQUAL "")
        message(FATAL_ERROR "Invalid run option wrote stdout: '${standard_output}'.")
    endif()
    if(NOT standard_error MATCHES "usage:")
        message(FATAL_ERROR "Invalid run option did not print usage: '${standard_error}'.")
    endif()
endfunction()

assert_usage_error(--jit-threshold=18446744073709551616)
assert_usage_error(--jit-threshold=1 --jit-threshold=2)
assert_usage_error(--trace-jit --trace-jit)
assert_usage_error(--unknown-option)
