/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTIC_MATH_H
#define ANOPTIC_MATH_H


/* Types */

// Shared linear-algebra value types (column-major float POD, trivially copyable, std430).
// Canonical across render, ECS, and the logic<->render bridge.
// Ops still in render/vertex.h until a non-render caller needs them.
// Each type carries its std430 base alignment.

// Column-major 4x4: m[i][j] = column i, row j. m[i] contiguous column, m[3] translation.
// Matches GLSL mat4 (memcpy upload, no transpose). See docs/math-conventions.md.
// Arg decays to float(*)[4], member is 64 bytes.
// Align attribute on the array type (C23 6.7.6 forbids it on typedef). Assertions below.
typedef float mat4[4][4] __attribute__((aligned(16)));

typedef struct Vector2
{
    alignas(8) float v[2];
} Vector2;

// Packed vertex-stream vec3 (Vertex, vertex.h). No std430 alignas(16).
// No GLSL block declares a vec3 member; use Vector4.
typedef struct Vector3
{
    float v[3];
} Vector3;

typedef struct Vector4
{
    alignas(16) float v[4];
} Vector4;

// std430 base alignments and sizes. A breach here silently shifts every mirrored GLSL block.
static_assert(sizeof(mat4) == 64 && alignof(mat4) == 16, "mat4 is not std430 (16-aligned, 64 bytes)");
static_assert(sizeof(Vector2) == 8 && alignof(Vector2) == 8, "Vector2 is not std430 vec2");
static_assert(sizeof(Vector3) == 12, "Vector3 is the packed vertex-stream vec3, 12 bytes");
static_assert(sizeof(Vector4) == 16 && alignof(Vector4) == 16, "Vector4 is not std430 vec4");

#endif // ANOPTIC_MATH_H
