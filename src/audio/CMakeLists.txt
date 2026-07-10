# Audio stack: bridge transport, mixer core, null device backend, WAV utility.
# Platform-agnostic and device-free so far: joins anoptic_core. Real device
# backends (audio_linux.c pipewire/alsa, audio_win64.c wasapi/dsound,
# audio_macos.c coreaudio) will join here behind the time.c-style
# if(WIN32)/elseif(APPLE)/elseif(UNIX) selection in later phases.
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ano_audio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_mixer.c
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_device_null.c
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_wav.c
)

# Per-platform device backends (APPLE before UNIX: both are true on macOS).
if (WIN32)
    # audio_win64.c (WASAPI + DirectSound) — Phase 5
elseif (APPLE)
    # audio_macos.c (CoreAudio) — Phase 5
elseif (UNIX)
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/audio_linux.c)
    # backends are dlopen'd — no link dep on pipewire/asound, just the loader
    target_link_libraries(anoptic_core PUBLIC ${CMAKE_DL_LIBS})
endif()

# sinf/cosf/expf live in a separate libm on Linux; MinGW and macOS fold it into libc.
if(UNIX AND NOT APPLE)
    target_link_libraries(anoptic_core PUBLIC m)
endif()
