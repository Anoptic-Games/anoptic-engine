/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "vertex.h"

// glibc hides M_PI when _POSIX_C_SOURCE is set without _DEFAULT_SOURCE
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

VkVertexInputBindingDescription getBindingDescription(void)
{
	VkVertexInputBindingDescription bindingDescription;
	bindingDescription.binding = 0;
	bindingDescription.stride = sizeof(Vertex);
	bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	return bindingDescription;
}

void getAttributeDescriptions(VkVertexInputAttributeDescription* attributeDescriptions)
{
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, position);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normal);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, texCoord);
}

void rotateMatrix(float mat[4][4], char axis, float angle) 
{
    float rotationMatrix[4][4] = 
	{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f}
    };
    
    float c = cosf(angle);
    float s = sinf(angle);

    switch(axis) 
	{
        case 'x':
        case 'X':
            rotationMatrix[1][1] = c;
            rotationMatrix[1][2] = s;
            rotationMatrix[2][1] = -s;
            rotationMatrix[2][2] = c;
            break;
        case 'y':
        case 'Y':
            rotationMatrix[0][0] = c;
            rotationMatrix[0][2] = -s;
            rotationMatrix[2][0] = s;
            rotationMatrix[2][2] = c;
            break;
        case 'z':
        case 'Z':
            rotationMatrix[0][0] = c;
            rotationMatrix[0][1] = s;
            rotationMatrix[1][0] = -s;
            rotationMatrix[1][1] = c;
            break;
        default:
            // Invalid axis
            return;
    }

    float result[4][4] = {0};
    for(int i = 0; i < 4; i++) 
	{
        for(int j = 0; j < 4; j++) 
		{
            for(int k = 0; k < 4; k++) 
			{
                result[i][j] += mat[i][k] * rotationMatrix[k][j];
            }
        }
    }

    // Copy result back into mat.
    for(int i = 0; i < 4; i++)
	{
        for(int j = 0; j < 4; j++) 
		{
            mat[i][j] = result[i][j];
        }
    }
}

void normalize(float vec[3])
{
    float length = sqrtf(vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2]);
    for (int i = 0; i < 3; ++i)
	{
        vec[i] /= length;
    }
}

void cross(const float a[3], const float b[3], float result[3])
{
    result[0] = a[1] * b[2] - a[2] * b[1];
    result[1] = a[2] * b[0] - a[0] * b[2];
    result[2] = a[0] * b[1] - a[1] * b[0];
}

float dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void lookAt(float mat[4][4], float eye[3], float center[3], float up[3])
{
    float f[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
    normalize(f);

    normalize(up);
    float s[3];
    cross(f, up, s);
    normalize(s);

    float u[3];
    cross(s, f, u);

    memset(mat, 0, sizeof(float) * 16);
    mat[0][0] = s[0];
    mat[1][0] = s[1];
    mat[2][0] = s[2];

    mat[0][1] = u[0];
    mat[1][1] = u[1];
    mat[2][1] = u[2];

    mat[0][2] = -f[0];
    mat[1][2] = -f[1];
    mat[2][2] = -f[2];

    mat[3][0] = -dot(s, eye);
    mat[3][1] = -dot(u, eye);
    mat[3][2] = dot(f, eye);
    mat[3][3] = 1.0f;
}

void translate(float mat[4][4], float x, float y, float z)
{
    // Identity matrix.
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            mat[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }

    // Translation components.
    mat[3][0] = x;
    mat[3][1] = y;
    mat[3][2] = z;
}

void perspective(float matrix[4][4], float fovDegrees, float aspect, float near, float far)
{
    float tanHalfFov = (float)tan(fovDegrees / 2.0f * (M_PI / 180.0f));

    // Initialize matrix to 0
    for (int i = 0; i < 4; ++i)
	{
        for (int j = 0; j < 4; ++j)
		{
            matrix[i][j] = 0.0f;
        }
    }

    matrix[0][0] = 1.0f / (aspect * tanHalfFov);
    matrix[1][1] = -1.0f / tanHalfFov; // Y flip for Vulkan!

	// Vulkan clip depth is [0,1] (ZO), not OpenGL's [-1,1]. RH, view looks down -Z.
	// ndc.z = (m22*z_view + m32) / (-z_view) maps z_view=-near -> 0, z_view=-far -> 1.
	matrix[2][2] = far / (near - far);
    matrix[2][3] = -1.0f;
	matrix[3][2] = (far * near) / (near - far);
	matrix[3][3] = 0.0f;

}

void multiplyMat4(mat4 result, const mat4 a, const mat4 b)
{
    mat4 temp = {0};
    for (int i = 0; i < 4; ++i) { // row
        for (int j = 0; j < 4; ++j) { // col
            temp[i][j] = 0.0f;
            for (int k = 0; k < 4; ++k) {
                // Column-major: temp[col][row] += a[k][row] * b[col][k].
                temp[i][j] += a[k][j] * b[i][k];
            }
        }
    }
    memcpy(result, temp, sizeof(mat4));
}

void extractFrustumPlanes(Vector4 planes[6], const mat4 viewProj)
{
    // Left
    planes[0].v[0] = viewProj[0][3] + viewProj[0][0];
    planes[0].v[1] = viewProj[1][3] + viewProj[1][0];
    planes[0].v[2] = viewProj[2][3] + viewProj[2][0];
    planes[0].v[3] = viewProj[3][3] + viewProj[3][0];

    // Right
    planes[1].v[0] = viewProj[0][3] - viewProj[0][0];
    planes[1].v[1] = viewProj[1][3] - viewProj[1][0];
    planes[1].v[2] = viewProj[2][3] - viewProj[2][0];
    planes[1].v[3] = viewProj[3][3] - viewProj[3][0];

    // Bottom
    planes[2].v[0] = viewProj[0][3] + viewProj[0][1];
    planes[2].v[1] = viewProj[1][3] + viewProj[1][1];
    planes[2].v[2] = viewProj[2][3] + viewProj[2][1];
    planes[2].v[3] = viewProj[3][3] + viewProj[3][1];

    // Top
    planes[3].v[0] = viewProj[0][3] - viewProj[0][1];
    planes[3].v[1] = viewProj[1][3] - viewProj[1][1];
    planes[3].v[2] = viewProj[2][3] - viewProj[2][1];
    planes[3].v[3] = viewProj[3][3] - viewProj[3][1];

    // Near. Vulkan [0,1] depth: near plane is clip.z >= 0, i.e. ROW 2 alone.
    planes[4].v[0] = viewProj[0][2];
    planes[4].v[1] = viewProj[1][2];
    planes[4].v[2] = viewProj[2][2];
    planes[4].v[3] = viewProj[3][2];

    // Far
    planes[5].v[0] = viewProj[0][3] - viewProj[0][2];
    planes[5].v[1] = viewProj[1][3] - viewProj[1][2];
    planes[5].v[2] = viewProj[2][3] - viewProj[2][2];
    planes[5].v[3] = viewProj[3][3] - viewProj[3][2];

    // Normalize
    for (int i = 0; i < 6; ++i) {
        float length = sqrtf(planes[i].v[0]*planes[i].v[0] + planes[i].v[1]*planes[i].v[1] + planes[i].v[2]*planes[i].v[2]);
        if (length > 0.0f) {
            planes[i].v[0] /= length;
            planes[i].v[1] /= length;
            planes[i].v[2] /= length;
            planes[i].v[3] /= length;
        }
    }
}

bool invertMat4(mat4 out, const mat4 m)
{
    // Cofactor (adjugate/determinant) inverse over the 16 contiguous floats. Element-wise, so
    // layout-agnostic. inv[] holds the transposed cofactors.
    const float *a = (const float *)m;
    float inv[16];

    inv[0]  =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
    inv[4]  = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
    inv[8]  =  a[4]*a[9]*a[15]  - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
    inv[12] = -a[4]*a[9]*a[14]  + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];

    inv[1]  = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
    inv[5]  =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
    inv[9]  = -a[0]*a[9]*a[15]  + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
    inv[13] =  a[0]*a[9]*a[14]  - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];

    inv[2]  =  a[1]*a[6]*a[15]  - a[1]*a[7]*a[14]  - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7]  - a[13]*a[3]*a[6];
    inv[6]  = -a[0]*a[6]*a[15]  + a[0]*a[7]*a[14]  + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7]  + a[12]*a[3]*a[6];
    inv[10] =  a[0]*a[5]*a[15]  - a[0]*a[7]*a[13]  - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7]  - a[12]*a[3]*a[5];
    inv[14] = -a[0]*a[5]*a[14]  + a[0]*a[6]*a[13]  + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6]  + a[12]*a[2]*a[5];

    inv[3]  = -a[1]*a[6]*a[11]  + a[1]*a[7]*a[10]  + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7]   + a[9]*a[3]*a[6];
    inv[7]  =  a[0]*a[6]*a[11]  - a[0]*a[7]*a[10]  - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7]   - a[8]*a[3]*a[6];
    inv[11] = -a[0]*a[5]*a[11]  + a[0]*a[7]*a[9]   + a[4]*a[1]*a[11] - a[4]*a[3]*a[9]  - a[8]*a[1]*a[7]   + a[8]*a[3]*a[5];
    inv[15] =  a[0]*a[5]*a[10]  - a[0]*a[6]*a[9]   - a[4]*a[1]*a[10] + a[4]*a[2]*a[9]  + a[8]*a[1]*a[6]   - a[8]*a[2]*a[5];

    float det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
    if (det == 0.0f) return false;
    det = 1.0f / det;

    float *o = (float *)out;
    for (int i = 0; i < 16; ++i) o[i] = inv[i] * det;
    return true;
}
