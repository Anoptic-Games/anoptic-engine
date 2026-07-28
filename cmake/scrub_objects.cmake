# scrub_objects.cmake: run via `cmake -DANO_SCRUB_DIR=<build dir> -P` (the ano_scrub target).
# Deletes every first-party object, leaves archives, .spv, staged assets, and configure state.
#
# The scrub decision lives here, so every caller agrees 〜 build.sh, build.bat, or a bare
# `cmake --build <dir> --target ano_scrub`:
#
#   ANO_SCRUB=1   scrub, whatever the config
#   ANO_SCRUB=0   skip, whatever the config
#   unset         ANO_SCRUB_DEFAULT, which the target sets to Release-only: LTO is
#                 Release-only (CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE), so that is
#                 where a whole build pays for itself. Debug and the sanitizer profiles
#                 go incremental; ninja recompiles anything whose command line moved, and
#                 build.sh already wipes the tree on a generator or source-root mismatch.

if(NOT ANO_SCRUB_DIR)
    message(FATAL_ERROR "ano_scrub: ANO_SCRUB_DIR not set")
endif()

# A direct `cmake -P` run is an explicit ask, so it scrubs unless told otherwise.
if(NOT DEFINED ANO_SCRUB_DEFAULT)
    set(ANO_SCRUB_DEFAULT 1)
endif()

set(ANO_SCRUB_ON "${ANO_SCRUB_DEFAULT}")
if(DEFINED ENV{ANO_SCRUB})
    set(ANO_SCRUB_ON "$ENV{ANO_SCRUB}")
    if(NOT ANO_SCRUB_ON MATCHES "^[01]$")
        message(FATAL_ERROR "ano_scrub: ANO_SCRUB must be 0 or 1 (got '$ENV{ANO_SCRUB}')")
    endif()
endif()

if(ANO_SCRUB_ON EQUAL 0)
    message(STATUS "ano_scrub: incremental 〜 ANO_SCRUB=1 forces a whole build")
    return()
endif()

file(GLOB_RECURSE ANO_OBJECTS
    "${ANO_SCRUB_DIR}/*.o"
    "${ANO_SCRUB_DIR}/*.obj")

# Pinned submodules (glfw, freetype, mimalloc) sit at fixed revisions and cannot drift
# against engine sources, so rebuilding them buys nothing. ~1/3 of the objects.
list(FILTER ANO_OBJECTS EXCLUDE REGEX "/external/")

list(LENGTH ANO_OBJECTS ANO_OBJECT_COUNT)
if(ANO_OBJECT_COUNT GREATER 0)
    file(REMOVE ${ANO_OBJECTS})
endif()
message(STATUS "ano_scrub: removed ${ANO_OBJECT_COUNT} object files from ${ANO_SCRUB_DIR}")
