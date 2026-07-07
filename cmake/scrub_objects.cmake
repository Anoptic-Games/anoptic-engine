# scrub_objects.cmake: run via `cmake -DANO_SCRUB_DIR=<build dir> -P` (the ano_scrub target).
# Deletes every compiled object, leaves archives, .spv, staged assets, and configure state.

if(NOT ANO_SCRUB_DIR)
    message(FATAL_ERROR "ano_scrub: ANO_SCRUB_DIR not set")
endif()

file(GLOB_RECURSE ANO_OBJECTS
    "${ANO_SCRUB_DIR}/*.o"
    "${ANO_SCRUB_DIR}/*.obj")

list(LENGTH ANO_OBJECTS ANO_OBJECT_COUNT)
if(ANO_OBJECT_COUNT GREATER 0)
    file(REMOVE ${ANO_OBJECTS})
endif()
message(STATUS "ano_scrub: removed ${ANO_OBJECT_COUNT} object files from ${ANO_SCRUB_DIR}")
