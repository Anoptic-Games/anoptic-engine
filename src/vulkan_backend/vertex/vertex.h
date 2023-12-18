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


// Variable types

typedef float mat4[4][4];


// Structs

typedef struct Vector2
{
	float v[2];
} Vector2;

typedef struct Vector3
{
	float v[3];
} Vector3;

typedef struct Vector4
{
	float v[4];
} Vector4;

typedef struct Vertex
{
	Vector3 position;
	Vector3 color;
	Vector2 texCoord;
} Vertex;


typedef struct UniformComponents
{
	mat4 model;
	mat4 view;
	mat4 proj;
} UniformComponents;

typedef struct ModelTransforms
{
	mat4 translation;
	mat4 rotation;
	mat4 scale;
} ModelTransforms;



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


#endif
