# UI module: primitive builder + reference evaluator, platform-agnostic C.
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ui_build.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ui_raster_ref.c
)
