if(NOT DEFINED SOURCE_FILE)
    message(FATAL_ERROR "SOURCE_FILE must be defined.")
endif()

function(assert_usage_error label expected_error)
    execute_process(
        COMMAND "${EMBER}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )

    if(NOT "${result}" STREQUAL "2")
        message(FATAL_ERROR
            "${label}: expected exit code 2, got '${result}': "
            "'${standard_error}'."
        )
    endif()
    if(NOT "${standard_output}" STREQUAL "")
        message(FATAL_ERROR
            "${label}: usage error wrote stdout: '${standard_output}'."
        )
    endif()
    if(NOT standard_error MATCHES "^error: ${expected_error}")
        message(FATAL_ERROR
            "${label}: unexpected error: '${standard_error}'."
        )
    endif()
    if(NOT standard_error MATCHES "usage: .*<command> \\[options\\] <file>")
        message(FATAL_ERROR
            "${label}: missing general usage: '${standard_error}'."
        )
    endif()
endfunction()

assert_usage_error(
    "run without a file"
    "run requires exactly one source file"
    run
)
assert_usage_error("duplicate run option" "--no-jit may be specified only once"
                   run --no-jit --no-jit "${SOURCE_FILE}")
assert_usage_error(
    "unknown run option"
    "unknown option '--unknown-option' for run"
    run --unknown-option "${SOURCE_FILE}"
)
assert_usage_error(
    "negative run threshold"
    "--jit-threshold must be a non-negative integer"
    run --jit-threshold=-1 "${SOURCE_FILE}"
)
assert_usage_error(
    "short run option"
    "unknown option '-x' for run"
    run -x "${SOURCE_FILE}"
)
assert_usage_error("multiple run files" "run accepts exactly one source file"
                   run "${SOURCE_FILE}" "${SOURCE_FILE}")
assert_usage_error(
    "dump without a file"
    "inspection commands require exactly one source file"
    dump-ast
)
assert_usage_error(
    "dump with multiple files"
    "inspection commands require exactly one source file"
    dump-ast "${SOURCE_FILE}" "${SOURCE_FILE}"
)
assert_usage_error(
    "unknown dump option"
    "unknown option '--unknown-option' for dump-ast"
    dump-ast --unknown-option
)
assert_usage_error(
    "short dump option"
    "unknown option '-x' for dump-ast"
    dump-ast -x
)
assert_usage_error(
    "benchmark without a file"
    "benchmark requires exactly one source file"
    benchmark
)
assert_usage_error(
    "invalid benchmark iteration count"
    "--iterations must be an integer from 1 through 100000"
    benchmark --iterations=0 "${SOURCE_FILE}"
)
assert_usage_error(
    "benchmark iteration count above limit"
    "--iterations must be an integer from 1 through 100000"
    benchmark --iterations=100001 "${SOURCE_FILE}"
)
assert_usage_error(
    "unknown benchmark option"
    "unknown option '--unknown-option' for benchmark"
    benchmark --unknown-option "${SOURCE_FILE}"
)
assert_usage_error(
    "duplicate benchmark iteration count"
    "--iterations must be an integer from 1 through 100000"
    benchmark --iterations=1 --iterations=2 "${SOURCE_FILE}"
)
assert_usage_error(
    "unknown help command"
    "help accepts at most one known command"
    help unknown-command
)
assert_usage_error(
    "version with an argument"
    "--version does not accept arguments"
    --version extra
)
