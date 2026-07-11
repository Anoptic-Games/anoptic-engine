# Music generation core (the TECH_SPEC port). Bit-parity with CPython demands
# stable float op order: no contraction anywhere in this module (§3.3, §8.4).
set(ANO_MUSIC_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/music_det.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_theory.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_harmony.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_voicing.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_ir.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_control.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_gen.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_motif.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_form.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_dramaturg.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_signatures.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_imitation.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_bass.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_melody.c
)
target_sources(anoptic_core PRIVATE ${ANO_MUSIC_SOURCES})
set_source_files_properties(${ANO_MUSIC_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-ffp-contract=off")
