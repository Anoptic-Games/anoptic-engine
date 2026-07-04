# Text module: platform-agnostic C over FreeType (linked PRIVATE in the root lists file).
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/text.c
        ${CMAKE_CURRENT_SOURCE_DIR}/text_bake.c
        ${CMAKE_CURRENT_SOURCE_DIR}/text_gpos.c
        ${CMAKE_CURRENT_SOURCE_DIR}/text_raster_ref.c
        ${CMAKE_CURRENT_SOURCE_DIR}/text_shape.c
)
