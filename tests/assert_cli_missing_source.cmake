function(assert_missing_source label)
    execute_process(
        COMMAND "${EMBER}" ${ARGN} definitely-missing-source.ember
        RESULT_VARIABLE result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )

    if(NOT "${result}" STREQUAL "1")
        message(FATAL_ERROR
            "${label}: expected exit code 1, got '${result}': "
            "'${standard_error}'."
        )
    endif()
    if(NOT "${standard_output}" STREQUAL "")
        message(FATAL_ERROR
            "${label}: missing file wrote stdout: '${standard_output}'."
        )
    endif()
    if(NOT standard_error MATCHES
       "^error: unable to read source file 'definitely-missing-source\\.ember'")
        message(FATAL_ERROR
            "${label}: unexpected error: '${standard_error}'."
        )
    endif()
endfunction()

assert_missing_source("run" run)
assert_missing_source("dump" dump-asm)
assert_missing_source("benchmark" benchmark)
