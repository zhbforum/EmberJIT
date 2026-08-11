foreach(required_variable IN ITEMS EMBER MODE TEST_TEMP_DIR)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} must be defined.")
    endif()
endforeach()

set(large_declaration_count 4096)

function(normalize_line_endings input_value output_variable)
    string(REPLACE "\r\n" "\n" normalized_value "${input_value}")
    set("${output_variable}" "${normalized_value}" PARENT_SCOPE)
endfunction()

function(invoke_ember command_name source_file output_prefix)
    if("${command_name}" STREQUAL "run")
        execute_process(
            COMMAND "${EMBER}" run --no-jit "${source_file}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE standard_output
            ERROR_VARIABLE standard_error
        )
    else()
        execute_process(
            COMMAND "${EMBER}" "${command_name}" "${source_file}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE standard_output
            ERROR_VARIABLE standard_error
        )
    endif()
    normalize_line_endings("${standard_output}" normalized_output)
    normalize_line_endings("${standard_error}" normalized_error)
    set("${output_prefix}_result" "${result}" PARENT_SCOPE)
    set("${output_prefix}_stdout" "${normalized_output}" PARENT_SCOPE)
    set("${output_prefix}_stderr" "${normalized_error}" PARENT_SCOPE)
endfunction()

function(write_large_source source_file include_malformed_tail)
    set(source "fn main() -> void {\n")
    foreach(index RANGE 1 ${large_declaration_count})
        string(APPEND source "    let value_${index}: i64 = ${index};\n")
    endforeach()

    if(include_malformed_tail)
        # The invalid byte comes only after the complete large valid prefix.
        string(APPEND source "    @\n}\n")
    else()
        string(APPEND source
            "    print_i64(value_${large_declaration_count});\n"
            "    return;\n"
            "}\n"
        )
    endif()
    file(WRITE "${source_file}" "${source}")
endfunction()

function(require_success label result output error expected)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR
            "${label}: expected exit code 0, got '${result}'.\n"
            "stderr:\n${error}"
        )
    endif()
    if(NOT "${error}" STREQUAL "")
        message(FATAL_ERROR "${label}: wrote stderr:\n${error}")
    endif()
    if(NOT "${output}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "${label}: unexpected stdout.\n"
            "expected:\n${expected}\n"
            "actual:\n${output}"
        )
    endif()
endfunction()

file(MAKE_DIRECTORY "${TEST_TEMP_DIR}")

if("${MODE}" STREQUAL "large-valid")
    set(source_file "${TEST_TEMP_DIR}/large_valid.ember")
    write_large_source("${source_file}" FALSE)
    invoke_ember(run "${source_file}" valid)
    set(expected_output "${large_declaration_count}\n")
    require_success(
        "large valid input"
        "${valid_result}"
        "${valid_stdout}"
        "${valid_stderr}"
        "${expected_output}"
    )
elseif("${MODE}" STREQUAL "large-malformed")
    set(source_file "${TEST_TEMP_DIR}/large_malformed.ember")
    write_large_source("${source_file}" TRUE)
    invoke_ember(dump-asm "${source_file}" first)
    invoke_ember(dump-asm "${source_file}" second)

    if(NOT "${first_result}" STREQUAL "1")
        message(FATAL_ERROR
            "first large malformed run: expected exit code 1, got "
            "'${first_result}'.\nstderr:\n${first_stderr}"
        )
    endif()
    if(NOT "${first_result}" STREQUAL "${second_result}")
        message(FATAL_ERROR
            "large malformed runs produced different exit codes: "
            "'${first_result}' and '${second_result}'."
        )
    endif()
    if(NOT "${first_stdout}" STREQUAL "")
        message(FATAL_ERROR
            "first large malformed run wrote stdout:\n${first_stdout}"
        )
    endif()
    if(NOT "${second_stdout}" STREQUAL "")
        message(FATAL_ERROR
            "second large malformed run wrote stdout:\n${second_stdout}"
        )
    endif()
    if("${first_stderr}" STREQUAL "")
        message(FATAL_ERROR "first large malformed run did not write stderr.")
    endif()
    if(NOT "${first_stderr}" STREQUAL "${second_stderr}")
        message(FATAL_ERROR
            "large malformed diagnostics differ.\n"
            "--- first ---\n${first_stderr}\n"
            "--- second ---\n${second_stderr}"
        )
    endif()
elseif("${MODE}" STREQUAL "hostile-path")
    set(hostile_directory "${TEST_TEMP_DIR}")
    foreach(segment_index RANGE 1 4)
        string(APPEND hostile_directory
            "/segment_${segment_index}_0123456789abcdef"
        )
    endforeach()
    file(MAKE_DIRECTORY "${hostile_directory}")
    set(source_file
        "${hostile_directory}/generated_source_0123456789abcdef.ember"
    )
    file(WRITE "${source_file}"
        "fn main() -> void {\n"
        "    print_i64(7);\n"
        "    return;\n"
        "}\n"
    )
    invoke_ember(run "${source_file}" hostile_path)
    require_success(
        "hostile path input"
        "${hostile_path_result}"
        "${hostile_path_stdout}"
        "${hostile_path_stderr}"
        "7\n"
    )
else()
    message(FATAL_ERROR "Unsupported MODE '${MODE}'.")
endif()
