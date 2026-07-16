# Synth: voice pool, patches, BeatClock schedule -> audio buses via generator. Platform-agnostic; audio DSP headers only.
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ano_synth.c
        ${CMAKE_CURRENT_SOURCE_DIR}/synth_voices.c
)
