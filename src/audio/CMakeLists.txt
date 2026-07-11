# Audio stack: bridge transport, mixer core, device backends, WAV utility.
# Joins anoptic_core; per-platform device files are selected below.
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ano_audio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_mixer.c
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_fx.c
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_device_null.c
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_wav.c
)

# Per-platform device backends (APPLE before UNIX: both are true on macOS).
if (WIN32)
    # WASAPI + DirectSound; ole32/avrt/dsound are runtime-loaded (no link deps)
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/audio_win64.c)
elseif (APPLE)
    # CoreAudio default-output AudioUnit
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/audio_macos.c)
    target_link_libraries(anoptic_core PUBLIC
            "-framework CoreAudio" "-framework AudioUnit" "-framework AudioToolbox")
elseif (UNIX)
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/audio_linux.c)
    # backends are dlopen'd — no link dep on pipewire/asound, just the loader
    target_link_libraries(anoptic_core PUBLIC ${CMAKE_DL_LIBS})
endif()

# sinf/cosf/expf live in a separate libm on Linux; MinGW and macOS fold it into libc.
if(UNIX AND NOT APPLE)
    target_link_libraries(anoptic_core PUBLIC m)
endif()
