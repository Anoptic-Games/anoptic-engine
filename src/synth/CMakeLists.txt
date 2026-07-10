# Synthesizer: voice pool + patches + BeatClock scheduler, rendering music IR
# (anoptic_music.h) into the audio module's buses through the generator seam.
# Platform-agnostic; depends on the audio module's DSP primitive headers only.
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ano_synth.c
        ${CMAKE_CURRENT_SOURCE_DIR}/synth_voices.c
)
