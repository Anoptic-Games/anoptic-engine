# Audio: bridge, mixer, device backends, WAV.
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ano_audio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_mixer.c
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_fx.c
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_device_null.c
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_wav.c
)

# Per-platform device (APPLE before UNIX).
if (WIN32)
    # WASAPI + DirectSound. ole32/avrt/dsound runtime-loaded.
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/audio_win64.c)
elseif (APPLE)
    # CoreAudio default-output AudioUnit
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/audio_macos.c)
    target_link_libraries(anoptic_core PUBLIC
            "-framework CoreAudio" "-framework AudioUnit" "-framework AudioToolbox")
elseif (UNIX)
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/audio_linux.c)
    # dlopen backends 〜 loader only
    target_link_libraries(anoptic_core PUBLIC ${CMAKE_DL_LIBS})
endif()

# libm on Linux. MinGW/macOS fold into libc.
if(UNIX AND NOT APPLE)
    target_link_libraries(anoptic_core PUBLIC m)
endif()
