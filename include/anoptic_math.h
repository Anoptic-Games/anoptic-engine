/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file anoptic_math.h
 * @brief Platform-agnostic linear-algebra value types shared engine-wide.
 *
 * These are the canonical definitions. The render module, the ECS, and the
 * logic<->render bridge all share them, so they live in the public include tree
 * rather than inside any one module. Storage is plain row-major float arrays so
 * the types are POD, trivially copyable, and byte-compatible with std430 GPU
 * layouts where the engine relies on that (transforms, light vectors).
 *
 * Math *operations* (multiply, perspective, lookAt, ...) currently live with the
 * render module in vertex.h; only the value types are centralized here. Move the
 * operations here too once a non-render caller needs them.
 */

#ifndef ANOPTIC_MATH_H
#define ANOPTIC_MATH_H

// Row-major 4x4 matrix. Array typedef: decays to float(*)[4] as a function arg,
// stored inline (64 bytes) as a struct/array member.
typedef float mat4[4][4];

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

#endif // ANOPTIC_MATH_H
