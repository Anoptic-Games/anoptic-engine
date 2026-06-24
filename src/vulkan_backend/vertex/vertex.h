/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Provides structures and function interfaces for handling vertex data */

#ifndef VERTEX_H
#define VERTEX_H

#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <anoptic_math.h>   // canonical mat4 / Vector2 / Vector3 / Vector4


// Structs

typedef struct Vertex
{
	Vector3 position;
	Vector3 normal;
	Vector2 texCoord;
} Vertex;


typedef struct GlobalUBO
{
	mat4 view;
	mat4 proj;
	float time;
	float deltaTime;
	uint32_t frameCount;
	uint32_t lightCount;   // number of active lights in the light SSBO (set 0, binding 8)
	float cameraPos[4];    // world-space camera position (xyz; w unused), avoids per-fragment inverse(view)
	// Clustered-forward froxel grid (see LIGHTING_SCALE.md). near/far + screen size map a
	// fragment to its froxel; the light-cull pass and the fragment shader must agree on these.
	// Two std140 16-byte rows; keep this tail in sync with every GlobalUBO GLSL mirror.
	float cameraNear;          // row: 160
	float cameraFar;           //      164
	float screenWidth;         //      168
	float screenHeight;        //      172
	uint32_t clusterDimX;      // row: 176
	uint32_t clusterDimY;      //      180
	uint32_t clusterDimZ;      //      184
	uint32_t maxLightsPerCluster; //   188  (== ANO_CLUSTER_MAX_LIGHTS)
	// Lighting/RC control row (RADIANCE_CASCADES.md). std140 16-byte row at offset 192; mirror
	// in the GlobalUBO declarations that read it (flat.frag, transmission.frag, lightcull.comp).
	uint32_t lightingMode;     // row: 192  (AnoLightingMode)
	uint32_t debugView;        //      196  (RC debug visualization; 0 = off)
	float    giStrength;       //      200  (RC GI ambient gain; runtime-tunable, RADIANCE_CASCADES.md R13)
	uint32_t pad1;             //      204
} GlobalUBO;



// Functions

// Determines the rate at which to load data from memory throughout the vertices
VkVertexInputBindingDescription getBindingDescription(void);

// Determines how to extract vertex attributes from vertex data chunks !NOTE this function expects an array of two attribute descriptions
void getAttributeDescriptions(VkVertexInputAttributeDescription*);

// Performs matrix rotation
void rotateMatrix(float mat[4][4], char axis, float angle);

// Creates a view matrix from the provided values
void lookAt(float mat[4][4], float eye[3], float center[3], float up[3]);

// Performs a simple 3D translation
void translate(float mat[4][4], float x, float y, float z);

// Creates a perspective matrix from the provided values
void perspective(float matrix[4][4], float fovDegrees, float aspect, float near, float far);

// Multiplies two 4x4 matrices: result = a * b
void multiplyMat4(mat4 result, const mat4 a, const mat4 b);

// Extracts the 6 frustum planes from a view-projection matrix
void extractFrustumPlanes(Vector4 planes[6], const mat4 viewProj);


#endif
