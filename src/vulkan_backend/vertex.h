/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Provides structures and function interfaces for handling vertex data */

#ifndef VERTEX_H
#define VERTEX_H

#include <vulkan/vulkan.h>
#include <stdlib.h>


// Structs

typedef struct Vector2
{
	float v[2];
} Vector2;

typedef struct Vector3
{
	float v[3];
} Vector3;

typedef struct Vertex
{
	Vector2 position;
	Vector3 color;
} Vertex;




// Functions

// Determines the rate at which to load data from memory throughout the vertices
VkVertexInputBindingDescription getBindingDescription(void);

// Determines how to extract vertex attributes from vertex data chunks !NOTE this function expects an array of two attribute descriptions
void getAttributeDescriptions(VkVertexInputAttributeDescription*);


#endif
