if(NOT DEFINED EMBER_SOURCE_DIR)
    message(FATAL_ERROR "EMBER_SOURCE_DIR is required")
endif()

function(assert_absent source_file pattern description)
    file(READ "${source_file}" contents)
    string(REGEX MATCH "${pattern}" forbidden_match "${contents}")
    if(forbidden_match)
        message(FATAL_ERROR
            "${description}: ${source_file} contains '${forbidden_match}'"
        )
    endif()
endfunction()

set(core_directory "${EMBER_SOURCE_DIR}/include/ember/core")
file(GLOB_RECURSE core_headers "${core_directory}/*.hpp")
if(core_headers STREQUAL "")
    message(FATAL_ERROR "No public core headers were found")
endif()

string(CONCAT forbidden_core_stage_include
    [[#[ \t]*include[ \t]*[<"]ember/]]
    [[(frontend|semantic|bytecode|ir|ssa|runtime|jit)/]]
)
foreach(header IN LISTS core_headers)
    assert_absent("${header}" "${forbidden_core_stage_include}"
                  "core headers must not depend on compiler stages")
endforeach()

assert_absent("${EMBER_SOURCE_DIR}/include/ember/runtime/native_value.hpp"
              "${forbidden_core_stage_include}"
              "native value header must not depend on compiler stages")

set(stage_public_headers)
foreach(stage IN ITEMS bytecode ir ssa runtime jit)
    set(stage_include_directory "${EMBER_SOURCE_DIR}/include/ember/${stage}")
    file(GLOB_RECURSE stage_headers "${stage_include_directory}/*.hpp")
    list(APPEND stage_public_headers ${stage_headers})
endforeach()

string(CONCAT forbidden_stage_dependency_include
    [[#[ \t]*include[ \t]*[<"]ember/]]
    [[(semantic|frontend)/]]
)
foreach(header IN LISTS stage_public_headers)
    assert_absent("${header}" "${forbidden_stage_dependency_include}"
                  "stage headers must not depend on semantic or frontend")
endforeach()

file(GLOB_RECURSE architecture_sources
    "${EMBER_SOURCE_DIR}/include/ember/*.hpp"
    "${EMBER_SOURCE_DIR}/src/*.cpp"
    "${EMBER_SOURCE_DIR}/tests/*.cpp"
    "${EMBER_SOURCE_DIR}/benchmarks/*.cpp"
)
string(CONCAT forbidden_legacy_semantic_symbol
    [[semantic::(Type|Function(Id|Kind|Signature)|typeName)]]
    [[([^A-Za-z0-9_]|$)]]
)
foreach(source_file IN LISTS architecture_sources)
    assert_absent("${source_file}"
                  "${forbidden_legacy_semantic_symbol}"
                  "shared language metadata must not remain in semantic")
    assert_absent("${source_file}" [[semantic::LiteralValue]]
                  "typed literals must use core values")
    assert_absent("${source_file}" [[bytecode::Value]]
                  "runtime values must use core values")
endforeach()
