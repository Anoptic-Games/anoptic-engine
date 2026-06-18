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
