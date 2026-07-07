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
#include <anoptic_math.h>   // mat4 / Vector2 / Vector3 / Vector4


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
	uint32_t lightCount;   // active lights in light SSBO (set 0, binding 8)
	float cameraPos[4];    // world-space camera position (xyz, w unused)
	// Clustered-forward froxel grid near/far + screen size, std140 rows.
	float cameraNear;          // row: 160
	float cameraFar;           //      164
	float screenWidth;         //      168
	float screenHeight;        //      172
	uint32_t clusterDimX;      // row: 176
	uint32_t clusterDimY;      //      180
	uint32_t clusterDimZ;      //      184
	uint32_t maxLightsPerCluster; //   188  (== ANO_CLUSTER_MAX_LIGHTS)
	// Lighting/RC control row, std140 offset 192.
	uint32_t lightingMode;     // row: 192  (AnoLightingMode)
	uint32_t debugView;        //      196  (RC debug visualization; 0 = off)
	uint32_t pad0;             //      200
	uint32_t pad1;             //      204
	// Camera clip transform + fragment unprojector.
	mat4 viewProj;             // row: 208  proj*view premultiplied
	mat4 invVPPixel;           //      272  inv(viewProj) * (pixel -> NDC)
	// Task-shader meshlet cull tail.
	float frustumPlanes[6][4]; // row: 336  this view's world-space frustum planes
	mat4  prevViewProj;        //      432  reprojection for the meshlet Hi-Z test
	float hizParams[4];        //      496  {pyramid baseW, baseH, mipCount (0 = off), 0}
	float hizProj[4];          //      512  {proj00, proj11, proj22, proj32}
} GlobalUBO;



// Functions

// Vertex input binding (load rate through the vertices).
VkVertexInputBindingDescription getBindingDescription(void);

// Extracts vertex attributes into an array of two attribute descriptions.
void getAttributeDescriptions(VkVertexInputAttributeDescription*);

// Performs matrix rotation
void rotateMatrix(float mat[4][4], char axis, float angle);

// Builds a view matrix.
void lookAt(float mat[4][4], float eye[3], float center[3], float up[3]);

// 3D translation.
void translate(float mat[4][4], float x, float y, float z);

// Builds a perspective matrix.
void perspective(float matrix[4][4], float fovDegrees, float aspect, float near, float far);

// Multiplies two 4x4 matrices: result = a * b
void multiplyMat4(mat4 result, const mat4 a, const mat4 b);

// Extracts the 6 frustum planes from a view-projection matrix
void extractFrustumPlanes(Vector4 planes[6], const mat4 viewProj);

// Inverts a 4x4 matrix. out = m^-1, returns false if m is singular.
bool invertMat4(mat4 out, const mat4 m);


#endif
