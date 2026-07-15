/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTIC_MATH_H
#define ANOPTIC_MATH_H


/* Types */

// Shared linear-algebra value types (row-major float POD, std430).
// Ops still in render/vertex.h until a non-render caller needs them.

// Row-major 4x4. Arg decays to float(*)[4], member is 64 bytes.
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
