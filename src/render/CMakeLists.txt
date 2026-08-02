# C++26 reflection and anogltf are the sole canonical glTF path.
target_sources(anoptic_render PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/gltf/anogltf.c
	${CMAKE_CURRENT_SOURCE_DIR}/gltf/ano_GltfParser_reflect.c
)
