target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ano_strings.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ano_strings_ops.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ano_strings_intern.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ano_strings_utf.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ano_strings_collate.c)