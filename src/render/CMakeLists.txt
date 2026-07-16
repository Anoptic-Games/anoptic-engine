# Add module-specific source files to the executable
target_sources(anoptic_render PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/gltf/ano_GltfParser.c
)
