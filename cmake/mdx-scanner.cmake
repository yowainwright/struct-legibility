# The upstream table omits MDX's leading JS tokens and trailing code-span tokens.
# Size it for the full enum and anchor the first initializer to LINE_ENDING.
file(READ "${scanner}" scanner_source)
set(original "static const bool paragraph_interrupt_symbols[] = {")
string(FIND "${scanner_source}" "${original}" table_offset)
if(table_offset EQUAL -1)
  message(FATAL_ERROR "MDX scanner changed; review the paragraph table fix")
endif()
set(corrected "static const bool paragraph_interrupt_symbols[UNCLOSED_SPAN + 1] = {\n    [LINE_ENDING] =")
string(REPLACE "${original}" "${corrected}" scanner_source "${scanner_source}")
set(scanner "${CMAKE_CURRENT_BINARY_DIR}/mdx-scanner.c")
file(CONFIGURE OUTPUT "${scanner}" CONTENT "${scanner_source}" @ONLY)
