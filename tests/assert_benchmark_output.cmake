execute_process(
    COMMAND "${EMBER}" benchmark --iterations=1 "${SOURCE_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
        "Expected exit code 0, got '${result}': '${standard_error}'."
    )
endif()
if(NOT "${standard_error}" STREQUAL "")
    message(FATAL_ERROR "Benchmark wrote stderr: '${standard_error}'.")
endif()
if(NOT standard_output MATCHES "benchmark: .*vm_hot_loop\\.ember")
    message(FATAL_ERROR "Benchmark header is missing: '${standard_output}'.")
endif()
if(NOT standard_output MATCHES "iterations: 1")
    message(FATAL_ERROR
        "Benchmark iteration count is missing: '${standard_output}'."
    )
endif()
if(NOT standard_output MATCHES "VM: [0-9]")
    message(FATAL_ERROR "VM measurement is missing: '${standard_output}'.")
endif()
if(NOT standard_output MATCHES
   "cold JIT \\(includes native compilation\\): [0-9]")
    message(FATAL_ERROR
        "Cold JIT measurement is missing: '${standard_output}'."
    )
endif()
if(NOT standard_output MATCHES "warmed JIT: [0-9]")
    message(FATAL_ERROR
        "Warmed JIT measurement is missing: '${standard_output}'."
    )
endif()
if(NOT standard_output MATCHES "reference measurements for this machine")
    message(FATAL_ERROR
        "Benchmark reference-measurement disclaimer is missing: "
        "'${standard_output}'."
    )
endif()
if(standard_output MATCHES "49995000")
    message(FATAL_ERROR
        "Benchmark leaked the measured program's stdout: '${standard_output}'."
    )
endif()
