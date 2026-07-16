# Music generation core. -ffp-contract=off: bit-parity with CPython float op order.
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
        ${CMAKE_CURRENT_SOURCE_DIR}/music_arp.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_perc.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_pad.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_counter.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_modifiers.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_conductor.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_verify.c
        ${CMAKE_CURRENT_SOURCE_DIR}/music_host.c
)
target_sources(anoptic_core PRIVATE ${ANO_MUSIC_SOURCES})
# TARGET_DIRECTORY is load-bearing: anoptic_core is defined at the root, and a
# source property set here without it is silently ignored. That no-op shipped
# fused fmadd on arm64 (baseline there, absent in baseline x86-64 codegen) and
# broke CPython bit-parity on macOS (anotest_music, 2026-07-16).
set_source_files_properties(${ANO_MUSIC_SOURCES}
        TARGET_DIRECTORY anoptic_core
        PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
