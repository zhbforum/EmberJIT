function(assert_success_contains label expected_pattern)
    execute_process(
        COMMAND "${EMBER}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )

    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR
            "${label}: expected exit code 0, got '${result}': "
            "'${standard_error}'."
        )
    endif()
    if(NOT "${standard_error}" STREQUAL "")
        message(FATAL_ERROR
            "${label}: wrote stderr: '${standard_error}'."
        )
    endif()
    if(NOT standard_output MATCHES "${expected_pattern}")
        message(FATAL_ERROR
            "${label}: unexpected stdout: '${standard_output}'."
        )
    endif()
endfunction()

assert_success_contains("no-argument help" "commands:")
assert_success_contains("global help" "benchmark" --help)
assert_success_contains("help command" "global options:" help)
assert_success_contains(
    "run help through help command"
    "usage: .* run"
    help run
)
assert_success_contains("run help" "--jit-threshold" run --help)
assert_success_contains("dump help" "usage: .* dump-asm <file>" dump-asm --help)
assert_success_contains(
    "benchmark help"
    "--iterations=<1-100000>"
    benchmark --help
)
assert_success_contains("version" "^EmberJIT 0\\.1\\.0" --version)
