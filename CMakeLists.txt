cmake_minimum_required(VERSION 3.24)

project(struct_legibility VERSION 0.1.0 LANGUAGES C)

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

add_library(struct_legibility STATIC
  src/analyzer.c
  src/files.c
  src/language.c
  src/languages/typescript.c
)
target_include_directories(struct_legibility PUBLIC include PRIVATE src)
target_link_libraries(struct_legibility PRIVATE tree-sitter tree_sitter_typescript Threads::Threads)
target_compile_definitions(struct_legibility PRIVATE _POSIX_C_SOURCE=200809L)
if(APPLE)
  target_compile_definitions(struct_legibility PRIVATE _DARWIN_C_SOURCE)
endif()

add_executable(struct-legibility src/cli.c)
target_link_libraries(struct-legibility PRIVATE struct_legibility)

if(SL_TQS_ROOT)
  if(SL_QUICKJS_ROOT)
    set(quickjs_dir ${SL_QUICKJS_ROOT})
  elseif(EXISTS ${SL_TQS_ROOT}/deps/quickjs-ng/quickjs.c)
    set(quickjs_dir ${SL_TQS_ROOT}/deps/quickjs-ng)
  else()
    FetchContent_Declare(
      quickjs_ng_source
      GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
      GIT_TAG d35af9df0d43db349621b9a1ede5d4a21b02c616
      SOURCE_SUBDIR source-only
    )
    FetchContent_MakeAvailable(quickjs_ng_source)
    set(quickjs_dir ${quickjs_ng_source_SOURCE_DIR})
  endif()
  add_executable(struct-legibility-qjsc EXCLUDE_FROM_ALL
    ${quickjs_dir}/qjsc.c
    ${quickjs_dir}/quickjs.c
    ${quickjs_dir}/quickjs-libc.c
    ${quickjs_dir}/libregexp.c
    ${quickjs_dir}/libunicode.c
    ${quickjs_dir}/dtoa.c
  )
  target_include_directories(struct-legibility-qjsc PRIVATE ${quickjs_dir})
  target_compile_definitions(struct-legibility-qjsc PRIVATE _GNU_SOURCE QUICKJS_NG_BUILD)
  target_link_libraries(struct-legibility-qjsc PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

if(SL_QUICKJS_SOURCE)
  if(NOT SL_TQS_ROOT)
    message(FATAL_ERROR "SL_TQS_ROOT is required with SL_QUICKJS_SOURCE")
  endif()
  add_library(struct_legibility_quickjs_bridge STATIC src/quickjs_bridge.c)
  target_include_directories(struct_legibility_quickjs_bridge PRIVATE include ${quickjs_dir} src)
  target_link_libraries(struct_legibility_quickjs_bridge PRIVATE struct_legibility)
  add_executable(struct-legibility-tqs EXCLUDE_FROM_ALL
    ${SL_QUICKJS_SOURCE}
    ${quickjs_dir}/quickjs.c
    ${quickjs_dir}/quickjs-libc.c
    ${quickjs_dir}/libregexp.c
    ${quickjs_dir}/libunicode.c
    ${quickjs_dir}/dtoa.c
  )
  target_include_directories(struct-legibility-tqs PRIVATE ${quickjs_dir} src)
  target_compile_definitions(struct-legibility-tqs PRIVATE _GNU_SOURCE QUICKJS_NG_BUILD)
  target_link_libraries(struct-legibility-tqs PRIVATE
    struct_legibility_quickjs_bridge
    Threads::Threads
    ${CMAKE_DL_LIBS}
    m
  )
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(struct_legibility PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(struct-legibility PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(TARGET struct_legibility_quickjs_bridge)
    target_compile_options(struct_legibility_quickjs_bridge PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endif()

if(BUILD_TESTING)
  add_executable(struct-legibility-api-test tests/c/api_test.c)
  target_link_libraries(struct-legibility-api-test PRIVATE struct_legibility)
  add_executable(struct-legibility-language-test tests/c/language_test.c)
  target_include_directories(struct-legibility-language-test PRIVATE
    src
    ${tree_sitter_SOURCE_DIR}/lib/include
  )
  target_link_libraries(struct-legibility-language-test PRIVATE struct_legibility)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(struct-legibility-api-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(struct-legibility-language-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME struct-legibility-api COMMAND struct-legibility-api-test)
  add_test(NAME struct-legibility-language COMMAND struct-legibility-language-test)
  add_test(
    NAME struct-legibility-cli
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/test-cli.sh
            $<TARGET_FILE:struct-legibility>
  )
endif()
