# Add module-specific source files to the executable
target_sources(anopticengine PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/gltf/ano_GltfParser.c
)
