# Logic<->render bridge transport. Platform-agnostic, GPU-free: joins anoptic_core.
target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ano_render_bridge.c)
