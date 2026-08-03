# C++26 reflection and value contracts are a header-only module.
add_library(anoptic_meta INTERFACE)
add_library(anoptic::meta ALIAS anoptic_meta)

target_sources(anoptic_meta INTERFACE
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_SOURCE_DIR}/include
        FILES
            ${CMAKE_SOURCE_DIR}/include/anoptic_meta.h
            ${CMAKE_SOURCE_DIR}/include/anoptic_meta_types.h
)

target_include_directories(anoptic_meta INTERFACE
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(anoptic_core PUBLIC anoptic::meta)
