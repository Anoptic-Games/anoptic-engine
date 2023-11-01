# Add module-specific source files to the executable
target_sources(anopticengine PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/vulkanMaster.c
	${CMAKE_CURRENT_SOURCE_DIR}/instance/instanceInit.c
	${CMAKE_CURRENT_SOURCE_DIR}/instance/pipeline.c
	${CMAKE_CURRENT_SOURCE_DIR}/vulkanConfig.c
	${CMAKE_CURRENT_SOURCE_DIR}/vertex/vertex.c
	${CMAKE_CURRENT_SOURCE_DIR}/texture/texture.c
)
