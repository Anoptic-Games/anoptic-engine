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
// Each type carries its std430 base alignment, so a C struct mirroring a GLSL block
// cannot land a member off-rule by accident of declaration order.

// Column-major 4x4: m[i][j] is column i, row j 〜 m[i] is a contiguous column, m[3] the
// translation. Matches GLSL's mat4, so an upload is a memcpy with no transpose. (Every C
// [4][4] stores m[i] contiguously; the convention is what m[i] names.) Conventions of
// record: docs/math-conventions.md. Arg decays to float(*)[4], member is 64 bytes.
// C forbids an alignment specifier on a typedef (C23 6.7.6), so the 16 std430 requires
// rides the array type as an attribute; the assertions below are the contract of record.
typedef float mat4[4][4] __attribute__((aligned(16)));

typedef struct Vector2
{
    alignas(8) float v[2];
} Vector2;

// The one type here that does NOT carry its std430 base alignment (16 for vec3): Vector3 is
// the tightly packed vertex-stream element (Vertex, vertex.h), where 16 would pad the stream
// by half for no GPU rule. No block in the tree declares a vec3 member; every one uses
// Vector4. A block that needs one must too.
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
