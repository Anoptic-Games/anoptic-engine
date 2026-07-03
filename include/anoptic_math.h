/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file anoptic_math.h
 * @brief Platform-agnostic linear-algebra value types shared engine-wide.
 *
 * Canonical definitions shared by render, ECS, and the logic<->render bridge.
 * Storage is row-major float arrays, POD, trivially copyable, std430-compatible.
 *
 * Math operations (multiply, perspective, lookAt, ...) still live in render's
 * vertex.h. Move them here once a non-render caller needs them.
 */

#ifndef ANOPTIC_MATH_H
#define ANOPTIC_MATH_H

// Row-major 4x4 matrix decaying to float(*)[4] as an arg, inline (64 bytes) as a member.
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
