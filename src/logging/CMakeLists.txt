# Only source needed.
target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/logging_core.c)

# The logger is held to a stricter bar than the rest of the tree: its printf-family format usage is
# fully checked and warnings are fatal. -Wformat-nonliteral forces every format string in this file
# to be a literal -- except the one deliberate vsnprintf forward in build_line, pragma-acknowledged
# at that line. Scoped to this translation unit only (TARGET_DIRECTORY anoptic_core), so the rest of
# the library keeps its current warning policy.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/logging_core.c
    TARGET_DIRECTORY anoptic_core
    PROPERTIES COMPILE_OPTIONS "-Wformat;-Wformat-nonliteral;-Werror")