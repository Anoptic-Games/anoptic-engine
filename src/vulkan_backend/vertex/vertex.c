/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "vertex.h"

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
    attributeDescriptions[1].offset = offsetof(Vertex, color);

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
            rotationMatrix[1][2] = -s;
            rotationMatrix[2][1] = s;
            rotationMatrix[2][2] = c;
            break;
        case 'y':
        case 'Y':
            rotationMatrix[0][0] = c;
            rotationMatrix[0][2] = s;
            rotationMatrix[2][0] = -s;
            rotationMatrix[2][2] = c;
            break;
        case 'z':
        case 'Z':
            rotationMatrix[0][0] = c;
            rotationMatrix[0][1] = -s;
            rotationMatrix[1][0] = s;
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

    // Copying the result back into the input matrix
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

void perspective(float matrix[4][4], float fovDegrees, float aspect, float near, float far)
{
    float tanHalfFov = (float)tan(fovDegrees / 2.0f * (M_PI / 180.0f));

    // Initialize matrix to identity
    for (int i = 0; i < 4; ++i)
	{
        for (int j = 0; j < 4; ++j)
		{
            matrix[i][j] = 0.0f;
        }
    }

    matrix[0][0] = 1.0f / (aspect * tanHalfFov);
    matrix[1][1] = 1.0f / tanHalfFov;
	matrix[2][2] = (far + near) / (near - far);

    matrix[2][3] = -1.0f;
	matrix[3][2] = (2 * far * near) / (near - far);
	matrix[3][3] = 1.0f;  // Set the homogenous coordinate to 1

}


