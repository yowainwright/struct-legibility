cmake_minimum_required(VERSION 3.24)

project(struct_lint VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)

include(FetchContent)
include(CTest)

FetchContent_Declare(
  tree_sitter
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  GIT_TAG 64402de2857cc197ecc4ca3bc144ea91fda7e72e
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tree_sitter)

FetchContent_Declare(
  tree_sitter_typescript_source
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-typescript.git
  GIT_TAG f975a621f4e7f532fe322e13c4f79495e0a7b2e7
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR source-only
)
FetchContent_MakeAvailable(tree_sitter_typescript_source)
find_package(Threads REQUIRED)

add_library(tree_sitter_typescript STATIC
  ${tree_sitter_typescript_source_SOURCE_DIR}/typescript/src/parser.c
  ${tree_sitter_typescript_source_SOURCE_DIR}/typescript/src/scanner.c
)
target_include_directories(tree_sitter_typescript PRIVATE
  ${tree_sitter_typescript_source_SOURCE_DIR}/typescript/src
)
target_link_libraries(tree_sitter_typescript PUBLIC tree-sitter)

add_library(struct_lint STATIC
  src/analyzer.c
  src/files.c
  src/language.c
  src/languages/typescript.c
)
target_include_directories(struct_lint PUBLIC include PRIVATE src)
target_link_libraries(struct_lint PRIVATE tree-sitter tree_sitter_typescript Threads::Threads)
target_compile_definitions(struct_lint PRIVATE _POSIX_C_SOURCE=200809L)
if(UNIX AND NOT APPLE)
  target_compile_definitions(struct_lint PRIVATE _XOPEN_SOURCE=700)
endif()
if(APPLE)
  target_compile_definitions(struct_lint PRIVATE _DARWIN_C_SOURCE)
endif()

add_executable(struct-lint src/cli.c)
target_link_libraries(struct-lint PRIVATE struct_lint)

add_library(struct_lint_scriptc STATIC src/scriptc_bridge.c)
target_include_directories(struct_lint_scriptc PRIVATE include)
target_link_libraries(struct_lint_scriptc PRIVATE struct_lint)

foreach(target struct-lint struct_lint_scriptc)
  target_compile_definitions(${target} PRIVATE SL_VERSION="${PROJECT_VERSION}")
endforeach()

file(GENERATE
  OUTPUT "${CMAKE_BINARY_DIR}/struct-lint-ffi.json"
  CONTENT "{
  \"ffi_format\": 1,
  \"functions\": [
    {
      \"name\": \"slScriptcInfo\",
      \"symbol\": \"sl_scriptc_info\",
      \"params\": [\"string\"],
      \"returns\": \"i32\"
    },
    {
      \"name\": \"slScriptcAnalyze\",
      \"symbol\": \"sl_scriptc_analyze\",
      \"params\": [\"string\", \"bool\", \"bool\", \"string\"],
      \"returns\": \"u32\"
    },
    {
      \"name\": \"slScriptcWrite\",
      \"symbol\": \"sl_scriptc_write\",
      \"params\": [\"string\", \"bool\"],
      \"returns\": \"u32\"
    }
  ],
  \"libraries\": [
    \"$<TARGET_FILE:struct_lint_scriptc>\",
    \"$<TARGET_FILE:struct_lint>\",
    \"$<TARGET_FILE:tree_sitter_typescript>\",
    \"$<TARGET_FILE:tree-sitter>\"
  ],
  \"system_libraries\": [\"pthread\"]
}
")

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(struct_lint PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(struct-lint PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(struct_lint_scriptc PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
  add_executable(struct-lint-api-test tests/c/api_test.c)
  target_link_libraries(struct-lint-api-test PRIVATE struct_lint)
  add_executable(struct-lint-language-test tests/c/language_test.c)
  target_include_directories(struct-lint-language-test PRIVATE
    src
    ${tree_sitter_SOURCE_DIR}/lib/include
  )
  target_link_libraries(struct-lint-language-test PRIVATE struct_lint)
  add_executable(struct-lint-allocation-test tests/c/allocation_test.c)
  target_include_directories(struct-lint-allocation-test PRIVATE
    ${tree_sitter_SOURCE_DIR}/lib/include
  )
  target_link_libraries(struct-lint-allocation-test PRIVATE struct_lint)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    foreach(test api language allocation)
      target_compile_options(struct-lint-${test}-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endforeach()
  endif()
  add_test(NAME struct-lint-api COMMAND struct-lint-api-test)
  add_test(NAME struct-lint-language COMMAND struct-lint-language-test)
  add_test(NAME struct-lint-allocation COMMAND struct-lint-allocation-test
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/typescript/function-order.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/typescript/section-order.ts
  )
  foreach(test options discovery)
    add_test(NAME struct-lint-${test}
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/test-${test}.sh
              $<TARGET_FILE:struct-lint>
    )
  endforeach()
  add_test(
    NAME struct-lint-cli
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/test-cli.sh
            $<TARGET_FILE:struct-lint>
  )
endif()
