# C++26 reflection path is canonical. The legacy importer and cgltf remain only as an oracle.
target_sources(anoptic_render PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/gltf/anogltf.c
	${CMAKE_CURRENT_SOURCE_DIR}/gltf/ano_GltfParser_reflect.c
)
