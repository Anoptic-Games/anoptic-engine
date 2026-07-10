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

# sinf/cosf/expf live in a separate libm on Linux; MinGW and macOS fold it into libc.
if(UNIX AND NOT APPLE)
    target_link_libraries(anoptic_core PUBLIC m)
endif()
