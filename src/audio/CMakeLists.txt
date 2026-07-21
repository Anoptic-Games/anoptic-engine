# Audio: the mixer and its sinks. Platform-free -- the NULL sink (an FNV byte oracle over
# every emitted frame) is the Stage A production sink, so there is no device backend to
# platformize yet. A real device sink lands behind the same interface and changes nothing above.
target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/audio.c
)
