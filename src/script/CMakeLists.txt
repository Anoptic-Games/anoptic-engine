# Script: the stack VM. Platform-free by construction -- a fuel cap and a bounds check are
# the same on every OS.
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/script.c
)
