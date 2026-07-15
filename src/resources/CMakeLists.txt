# Resource manager: platform-free core + one OS TU per platform.
# APPLE before UNIX: both are true on macOS, and the POSIX TU covers both anyway.
if (WIN32)
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/resources_win64.c
    )
else ()
    target_sources(anoptic_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/resources_posix.c
    )
endif ()

# Core split by workstream:
#   resources_core.c      paths, mounts, write protocol, saves
#   resources_read.c      candidate walk, sinks, source dispatch, ranges
#   resources_registry.c  rows, domains, publication, epochs
#   resources_ident.c     rids (file / derived / duplicate)
#   resources_ext.c       extension + kind registry (fourcc -> dense id)
#   resources_place.c     placement seam
#   resources_models.c    model literals only
#   resources_tel.c       five-axis attribution cells
#   resources_block.c     block framing and hostile-input gate
#   resources_stream.c    streaming chunks through placement
#   resources_reload.c    hot reload
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/resources_core.c
        ${CMAKE_CURRENT_SOURCE_DIR}/resources_read.c
        ${CMAKE_CURRENT_SOURCE_DIR}/resources_registry.c
        ${CMAKE_CURRENT_SOURCE_DIR}/resources_ident.c
        ${CMAKE_CURRENT_SOURCE_DIR}/resources_ext.c
        ${CMAKE_CURRENT_SOURCE_DIR}/resources_place.c
        ${CMAKE_CURRENT_SOURCE_DIR}/resources_models.c
        ${CMAKE_CURRENT_SOURCE_DIR}/resources_tel.c
        ${CMAKE_CURRENT_SOURCE_DIR}/resources_block.c
        ${CMAKE_CURRENT_SOURCE_DIR}/resources_stream.c
        ${CMAKE_CURRENT_SOURCE_DIR}/resources_reload.c
)

# Domain extensions. Each is a res_ext registration.
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/res_graphics.c
        ${CMAKE_CURRENT_SOURCE_DIR}/text/res_font.c
        ${CMAKE_CURRENT_SOURCE_DIR}/world/res_world.c
        ${CMAKE_CURRENT_SOURCE_DIR}/world/res_level.c
)

# Codec + pack. LZ4 vendored always on. zstd is option(ANOPTIC_ZSTD OFF).
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/codec/res_codec.c
        ${CMAKE_CURRENT_SOURCE_DIR}/pack/res_pack.c
)
target_link_libraries(anoptic_core PRIVATE anoptic_lz4)
if (ANOPTIC_ZSTD)
    target_link_libraries(anoptic_core PRIVATE anoptic_zstd)
endif ()
