foreach(required_variable IN ITEMS EMBER COMMAND_NAME SOURCE_FILE MODE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} must be defined.")
    endif()
endforeach()

# The Windows C runtime writes text streams with CRLF, while supported Unix
# targets write LF. Normalize only that platform newline representation; all
# other bytes, including whitespace and the final newline, remain exact.
function(normalize_line_endings input_value output_variable)
    string(REPLACE "\r\n" "\n" normalized_value "${input_value}")
    set("${output_variable}" "${normalized_value}" PARENT_SCOPE)
endfunction()

function(assert_exact label expected actual)
    if("${expected}" STREQUAL "${actual}")
        return()
    endif()

    string(LENGTH "${expected}" expected_length)
    string(LENGTH "${actual}" actual_length)
    if(expected_length LESS actual_length)
        set(common_length "${expected_length}")
    else()
        set(common_length "${actual_length}")
    endif()

    set(first_mismatch "${common_length}")
    if(common_length GREATER 0)
        math(EXPR last_index "${common_length} - 1")
        foreach(index RANGE 0 ${last_index})
            string(SUBSTRING "${expected}" ${index} 1 expected_byte)
            string(SUBSTRING "${actual}" ${index} 1 actual_byte)
            if(NOT "${expected_byte}" STREQUAL "${actual_byte}")
                set(first_mismatch "${index}")
                break()
            endif()
        endforeach()
    endif()

    message(FATAL_ERROR
        "${label} differs at normalized byte ${first_mismatch} "
        "(expected ${expected_length}, actual ${actual_length}).\n"
        "--- expected ---\n${expected}\n"
        "--- actual ---\n${actual}\n"
    )
endfunction()

function(invoke_ember output_prefix)
    execute_process(
        COMMAND "${EMBER}" "${COMMAND_NAME}" "${SOURCE_FILE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )
    normalize_line_endings("${standard_output}" normalized_output)
    normalize_line_endings("${standard_error}" normalized_error)
    set("${output_prefix}_result" "${result}" PARENT_SCOPE)
    set("${output_prefix}_stdout" "${normalized_output}" PARENT_SCOPE)
    set("${output_prefix}_stderr" "${normalized_error}" PARENT_SCOPE)
endfunction()

if("${MODE}" STREQUAL "golden")
    if(NOT DEFINED GOLDEN_FILE)
        message(FATAL_ERROR "GOLDEN_FILE must be defined for golden mode.")
    endif()

    invoke_ember(golden)
    if(NOT "${golden_result}" STREQUAL "0")
        message(FATAL_ERROR
            "${COMMAND_NAME}: expected exit code 0, got '${golden_result}'.\n"
            "stderr:\n${golden_stderr}"
        )
    endif()
    if(NOT "${golden_stderr}" STREQUAL "")
        message(FATAL_ERROR
            "${COMMAND_NAME}: successful dump wrote stderr:\n${golden_stderr}"
        )
    endif()

    file(READ "${GOLDEN_FILE}" golden_output)
    normalize_line_endings("${golden_output}" normalized_golden_output)
    assert_exact(
        "${COMMAND_NAME} output"
        "${normalized_golden_output}"
        "${golden_stdout}"
    )
elseif("${MODE}" STREQUAL "repeated-invalid")
    if(NOT DEFINED EXPECTED_EXIT_CODE)
        message(FATAL_ERROR
            "EXPECTED_EXIT_CODE must be defined for repeated-invalid mode."
        )
    endif()

    invoke_ember(first)
    invoke_ember(second)

    if(NOT "${first_result}" STREQUAL "${EXPECTED_EXIT_CODE}")
        message(FATAL_ERROR
            "first invalid run: expected exit code ${EXPECTED_EXIT_CODE}, "
            "got '${first_result}'.\nstderr:\n${first_stderr}"
        )
    endif()
    if(NOT "${first_result}" STREQUAL "${second_result}")
        message(FATAL_ERROR
            "invalid runs produced different exit codes: '${first_result}' and "
            "'${second_result}'."
        )
    endif()
    if(NOT "${first_stdout}" STREQUAL "")
        message(FATAL_ERROR
            "first invalid run wrote a partial dump to stdout:\n${first_stdout}"
        )
    endif()
    if(NOT "${second_stdout}" STREQUAL "")
        message(FATAL_ERROR
            "second invalid run wrote a partial dump to stdout:\n"
            "${second_stdout}"
        )
    endif()
    if("${first_stderr}" STREQUAL "")
        message(FATAL_ERROR "first invalid run did not write a diagnostic.")
    endif()

    assert_exact("invalid-run stdout" "${first_stdout}" "${second_stdout}")
    assert_exact("invalid-run stderr" "${first_stderr}" "${second_stderr}")
else()
    message(FATAL_ERROR "Unsupported MODE '${MODE}'.")
endif()
