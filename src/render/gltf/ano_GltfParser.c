/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "ano_GltfParser.h"
#include "jsmn.h"

// Utility functions

// This might seem dubious, but trust me
uint32_t keyHash(const char *str)
{
	uint32_t hash = 0;
	int c;

	while ((c = *str++))
	{
		hash += c;
	}

	return hash;
}

// Parsing functions

bool countGltfElements(const char *json, GltfElements *elements)
{
	jsmn_parser parser;
	jsmntok_t *tokens;
	int tokenCount;

	jsmn_init(&parser);

	// First pass to count the number of tokens
	tokenCount = jsmn_parse(&parser, json, strlen(json), NULL, 0);

	if (tokenCount < 0)
	{
		return false;  // Parsing failed.
	}

	tokens = malloc(sizeof(jsmntok_t) * tokenCount);

	// Second pass to actually parse the JSON
	jsmn_parse(&parser, json, strlen(json), tokens, tokenCount);

	for (int i = 0; i < tokenCount - 1; i++)  // -1 to prevent going out of bounds
	{
		if (tokens[i].type == JSMN_STRING)
		{
			// Create a temporary string for the key
			int keyLength = tokens[i].end - tokens[i].start;
			char key[keyLength + 1];
			memcpy(key, &json[tokens[i].start], keyLength);
			key[keyLength] = '\0';

			uint32_t hash = keyHash(key);

			// Use switch statement for hashed key values
			switch(hash)
			{
				case GLTF_SCENES:
					elements->sceneCount = tokens[i + 1].size;
					break;
				case GLTF_NODES:
					elements->nodeCount = tokens[i + 1].size;
					break;
				case GLTF_MATERIALS:
					elements->materialCount = tokens[i + 1].size;
					break;
				case GLTF_MESHES:
					elements->meshCount = tokens[i + 1].size;
					break;
				case GLTF_TEXTURES:
					elements->textureCount = tokens[i + 1].size;
					break;
				case GLTF_IMAGES:
					elements->imageCount = tokens[i + 1].size;
					break;
				case GLTF_ACCESSORS:
					elements->accessorCount = tokens[i + 1].size;
					break;
				case GLTF_BUFFERVIEWS:
					elements->bufferViewCount = tokens[i + 1].size;
					break;
				case GLTF_SAMPLERS:
					elements->samplerCount = tokens[i + 1].size;
					break;
				case GLTF_BUFFERS:
					elements->bufferCount = tokens[i + 1].size;
					break;
				// Add cases for other keys as needed
				default:
					// Handle unknown or unhandled keys
					printf("Encountered unknown element in glTF file!\n");
					return false;
					break;
			}
		}
	}

	free(tokens);

	return true;
}

void initializeGltfElements(GltfElements *elements)
{
	if (elements == NULL)
	{
		// Handle null pointer if necessary
		return;
	}

	// Allocate memory for scenes
	if (elements->sceneCount > 0)
	{
		elements->scenes = (GltfScene*)malloc(elements->sceneCount * sizeof(GltfScene));
	}

	// Allocate memory for nodes
	if (elements->nodeCount > 0)
	{
		elements->nodes = (GltfNode*)malloc(elements->nodeCount * sizeof(GltfNode));
	}

	// Allocate memory for materials
	if (elements->materialCount > 0)
	{
		elements->materials = (GltfMaterial*)malloc(elements->materialCount * sizeof(GltfMaterial));
	}

	// Allocate memory for meshes
	if (elements->meshCount > 0)
	{
		elements->meshes = (GltfMesh*)malloc(elements->meshCount * sizeof(GltfMesh));
	}

	// Allocate memory for textures
	if (elements->textureCount > 0)
	{
		elements->textures = (GltfTexture*)malloc(elements->textureCount * sizeof(GltfTexture));
	}

	// Allocate memory for images
	if (elements->imageCount > 0)
	{
		elements->images = (GltfImage*)malloc(elements->imageCount * sizeof(GltfImage));
	}

	// Allocate memory for accessors
	if (elements->accessorCount > 0)
	{
		elements->accessors = (GltfAccessor*)malloc(elements->accessorCount * sizeof(GltfAccessor));
	}

	// Allocate memory for bufferViews
	if (elements->bufferViewCount > 0)
	{
		elements->bufferViews = (GltfBufferView*)malloc(elements->bufferViewCount * sizeof(GltfBufferView));
	}

	// Allocate memory for samplers
	if (elements->samplerCount > 0)
	{
		elements->samplers = (GltfSampler*)malloc(elements->samplerCount * sizeof(GltfSampler));
	}

	// Allocate memory for buffers
	if (elements->bufferCount > 0)
	{
		elements->buffers = (GltfBuffer*)malloc(elements->bufferCount * sizeof(GltfBuffer));
	}

	// Add error checking after each allocation if you need to handle allocation failures
}

void helperParseScenes(const char *json, jsmntok_t *tokens, GltfScene *scenes)
{
	// Assuming scenes is an array of GltfScene structs with enough space

	// Iterate over each scene in the scenes array
	int sceneIndex = 0; // Index for the scenes array
	for (int i = 0; i < tokens->size; i++)
	{
		jsmntok_t *token = &tokens[i];

		// Check if the token is of type JSMN_OBJECT
		if (token->type != JSMN_OBJECT)
		{
			// Handle error: not an object
			continue;
		}

		// Initialize the nodeCount for the current scene
		scenes[sceneIndex].nodeCount = 0;

		// Iterate over the key-value pairs within the object
		for (int j = 0; j < token->size; j++)
		{
			// Calculate the key and value token indices
			int key_token_index = i * 2 + 1;
			jsmntok_t key_token = tokens[key_token_index];
			int value_token_index = key_token_index + 1;
			jsmntok_t value_token = tokens[value_token_index];

			// Extract the key string
			int key_length = key_token.end - key_token.start;
			char key[key_length + 1]; // +1 for the null terminator
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0'; // Null terminate the string

			uint32_t hash = keyHash(key);

			// Use switch statement for hashed key values
			switch(hash)
			{
				case GLTF_NAME:
					// Handle the "name" key
					// ... (name handling code remains the same) ...
					break;
				case GLTF_NODES:
					// Handle the "nodes" key, which is expected to be an array
					if (value_token.type == JSMN_ARRAY)
					{
						// Set the nodeCount for the current scene
						scenes[sceneIndex].nodeCount = value_token.size;

						// Allocate memory for the nodes array
						scenes[sceneIndex].nodes = malloc(sizeof(uint32_t) * value_token.size);

						// Fill the nodes array with node indices
						for (int k = 0; k < value_token.size; k++)
						{
							int node_index = value_token_index + 1 + k;
							int node_value_length = tokens[node_index].end - tokens[node_index].start;
							char node_value_str[node_value_length + 1];
							memcpy(node_value_str, json + tokens[node_index].start, node_value_length);
							node_value_str[node_value_length] = '\0';

							scenes[sceneIndex].nodes[k] = (uint32_t)strtol(node_value_str, NULL, 10);
						}
					}
					break;
				// Add more cases for other keys if needed
				default:
					// Handle unknown or unhandled keys
					break;
			}
		}

		sceneIndex++;
	}
}

void helperParseNodes(const char *json, jsmntok_t *tokens, GltfNode *nodes)
{
	// Iterate over each node in the nodes array
	int nodeIndex = 0;
	for (int i = 0; i < tokens->size; i++)
	{
		jsmntok_t *token = &tokens[i];

		// Check if the token is of type JSMN_OBJECT
		if (token->type != JSMN_OBJECT)
		{
			continue; // Skip non-object tokens
		}

		// Initialize the GltfNode
		nodes[nodeIndex].mesh = 0;
		nodes[nodeIndex].name = NULL;
		nodes[nodeIndex].rotation = (Vector4){0, 0, 0, 0};

		// Iterate over the key-value pairs within the object
		for (int j = 0; j < token->size; j++)
		{
			int key_token_index = i * 2 + 1;
			jsmntok_t key_token = tokens[key_token_index];
			int value_token_index = key_token_index + 1;
			jsmntok_t value_token = tokens[value_token_index];

			// Extract and hash the key string
			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);
			int name_length;
			int element_index;
			switch(hash)
			{
				case GLTF_MESH:
					// Parse the "mesh" key
					nodes[nodeIndex].mesh = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				case GLTF_NAME:
					// Parse the "name" key
					name_length = value_token.end - value_token.start;
					nodes[nodeIndex].name = malloc(name_length + 1);
					memcpy(nodes[nodeIndex].name, json + value_token.start, name_length);
					nodes[nodeIndex].name[name_length] = '\0';
					break;

				case GLTF_ROTATION:
					// Parse the "rotation" key, expected to be an array
					if (value_token.type == JSMN_ARRAY)
					{
						for (int k = 0; k < value_token.size; k++)
						{
							element_index = value_token_index + 1 + k;
							nodes[nodeIndex].rotation.v[k] = atof(json + tokens[element_index].start);
						}
					}
					break;

				default:
					// Handle unknown or unhandled keys
					break;
			}
		}

		nodeIndex++;
	}
}

void parsePbrMetallicRoughness(const char *json, jsmntok_t *tokens, PbrMetallicRoughness *pbr)
{
	for (int i = 0; i < tokens->size; i++)
	{
		jsmntok_t *token = &tokens[i];

		if (token->type != JSMN_OBJECT)
		{
			continue;
		}

		for (int j = 0; j < token->size; j++)
		{
			int key_token_index = i * 2 + 1;
			jsmntok_t key_token = tokens[key_token_index];
			int value_token_index = key_token_index + 1;
			jsmntok_t value_token = tokens[value_token_index];

			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch(hash)
			{
				case GLTF_BASECOLORTEXTURE:
					pbr->baseColorTexture = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				case GLTF_METALLICFACTOR:
					pbr->metallicFactor = atof(json + value_token.start);
					break;

				case GLTF_ROUGHNESSFACTOR:
					pbr->roughnessFactor = atof(json + value_token.start);
					break;

				default:
					break;
			}
		}
	}
}

void helperParseMaterials(const char *json, jsmntok_t *tokens, GltfMaterial *materials)
{
	int materialIndex = 0;
	int name_length = 0;
	for (int i = 0; i < tokens->size; i++)
	{
		jsmntok_t *token = &tokens[i];

		// Check if the token is of type JSMN_OBJECT
		if (token->type != JSMN_OBJECT)
		{
			continue;
		}

		// Initialize the GltfMaterial
		materials[materialIndex].doubleSided = false;
		materials[materialIndex].name = NULL;
		materials[materialIndex].pbr = (PbrMetallicRoughness){0, 0.0f, 0.0f};

		for (int j = 0; j < token->size; j++)
		{
			int key_token_index = i * 2 + 1;
			jsmntok_t key_token = tokens[key_token_index];
			int value_token_index = key_token_index + 1;
			jsmntok_t value_token = tokens[value_token_index];

			// Extract and hash the key string
			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch(hash)
			{
				case GLTF_DOUBLESIDED:
					// Parse the "doubleSided" key
					materials[materialIndex].doubleSided = (strncmp(json + value_token.start, "true", 4) == 0);
					break;

				case GLTF_NAME:
					// Parse the "name" key
					name_length = value_token.end - value_token.start;
					materials[materialIndex].name = malloc(name_length + 1);
					memcpy(materials[materialIndex].name, json + value_token.start, name_length);
					materials[materialIndex].name[name_length] = '\0';
					break;

				case GLTF_PBRMETALICROUGHNESS:
					// Parse the "pbrMetallicRoughness" object
					if (value_token.type == JSMN_OBJECT)
					{
						jsmntok_t *pbrTokens = &tokens[value_token_index + 1];
						parsePbrMetallicRoughness(json, pbrTokens, &materials[materialIndex].pbr);
					}
					break;

				default:
					// Handle unknown or unhandled keys
					break;
			}
		}

		materialIndex++;
	}

	// Additional parsing logic for nested PbrMetallicRoughness properties
	// ...
}

void parsePrimitive(const char *json, jsmntok_t *tokens, GltfPrimitive *primitive)
{
	for (int i = 0; i < tokens->size; i++)
	{
		jsmntok_t *token = &tokens[i];

		if (token->type != JSMN_OBJECT)
		{
			continue;
		}

		for (int j = 0; j < token->size; j++)
		{
			int key_token_index = i * 2 + 1;
			jsmntok_t key_token = tokens[key_token_index];
			int value_token_index = key_token_index + 1;
			jsmntok_t value_token = tokens[value_token_index];

			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			uint32_t value = (uint32_t)strtol(json + value_token.start, NULL, 10);
			switch(hash)
			{
				case GLTF_POSITION:
					primitive->position = value;
					break;
				case GLTF_NORMAL:
					primitive->normal = value;
					break;
				case GLTF_TEXCOORD_0:
					primitive->texcoord = value;
					break;
				case GLTF_INDICES:
					primitive->indices = value;
					break;
				case GLTF_MATERIAL:
					primitive->material = value;
					break;
				default:
					break;
			}
		}
	}
}

void helperParseMeshes(const char *json, jsmntok_t *tokens, GltfMesh *meshes)
{
	int meshIndex = 0;
	int name_length = 0;
	for (int i = 0; i < tokens->size; i++)
	{
		jsmntok_t *token = &tokens[i];

		if (token->type != JSMN_OBJECT)
		{
			continue;
		}

		// Initialize the GltfMesh
		meshes[meshIndex].name = NULL;
		memset(&meshes[meshIndex].primitives, 0, sizeof(GltfPrimitive));

		for (int j = 0; j < token->size; j++)
		{
			int key_token_index = i * 2 + 1;
			jsmntok_t key_token = tokens[key_token_index];
			int value_token_index = key_token_index + 1;
			jsmntok_t value_token = tokens[value_token_index];

			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch(hash)
			{
				case GLTF_NAME:
					name_length = value_token.end - value_token.start;
					meshes[meshIndex].name = malloc(name_length + 1);
					memcpy(meshes[meshIndex].name, json + value_token.start, name_length);
					meshes[meshIndex].name[name_length] = '\0';
					break;

				case GLTF_PRIMITIVES:
					if (value_token.type == JSMN_ARRAY)
					{
						// Assuming each mesh has only one primitive for now
						jsmntok_t *primitiveToken = &tokens[value_token_index + 1];
						parsePrimitive(json, primitiveToken, &meshes[meshIndex].primitives);
					}
					break;

				default:
					break;
			}
		}

		meshIndex++;
	}
}

void helperParseTextures(const char *json, jsmntok_t *tokens, GltfTexture *textures)
{
	int textureIndex = 0;
	for (int i = 0; i < tokens->size; i++)
	{
		jsmntok_t *token = &tokens[i];

		// Check if the token is of type JSMN_OBJECT
		if (token->type != JSMN_OBJECT)
		{
			continue;
		}

		// Initialize the GltfTexture
		textures[textureIndex].sampler = 0;
		textures[textureIndex].source = 0;

		for (int j = 0; j < token->size; j++)
		{
			int key_token_index = i * 2 + 1;
			jsmntok_t key_token = tokens[key_token_index];
			int value_token_index = key_token_index + 1;
			jsmntok_t value_token = tokens[value_token_index];

			// Extract and hash the key string
			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch(hash)
			{
				case GLTF_SAMPLER:
					textures[textureIndex].sampler = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				case GLTF_SOURCE:
					textures[textureIndex].source = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				default:
					// Handle unknown or unhandled keys
					break;
			}
		}

		textureIndex++;
	}
}

char* extractJsonString(const char *json, jsmntok_t *token)
{
	int length = token->end - token->start;
	char *str = malloc(length + 1);
	memcpy(str, json + token->start, length);
	str[length] = '\0';
	return str;
}

void helperParseImages(const char *json, jsmntok_t *tokens, GltfImage *images)
{
	int imageIndex = 0;
	for (int i = 0; i < tokens->size; i++)
	{
		jsmntok_t *token = &tokens[i];

		// Check if the token is of type JSMN_OBJECT
		if (token->type != JSMN_OBJECT)
		{
			continue;
		}

		// Initialize the GltfImage
		images[imageIndex].mimeType = NULL;
		images[imageIndex].name = NULL;
		images[imageIndex].uri = NULL;

		for (int j = 0; j < token->size; j++)
		{
			int key_token_index = i * 2 + 1;
			jsmntok_t key_token = tokens[key_token_index];
			int value_token_index = key_token_index + 1;
			jsmntok_t value_token = tokens[value_token_index];

			// Extract and hash the key string
			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch(hash)
			{
				case GLTF_MIMETYPE:
					images[imageIndex].mimeType = extractJsonString(json, &value_token);
					break;

				case GLTF_NAME:
					images[imageIndex].name = extractJsonString(json, &value_token);
					break;

				case GLTF_URI:
					images[imageIndex].uri = extractJsonString(json, &value_token);
					break;

				default:
					// Handle unknown or unhandled keys
					break;
			}
		}

		imageIndex++;
	}
}

void parseAccessorMaxMin(const char *json, jsmntok_t *tokens, float *array, int size)
{
	for (int i = 0; i < size; i++)
	{
		array[i] = atof(json + tokens[i].start);
	}
}

void helperParseAccessors(const char *json, jsmntok_t *tokens, GltfAccessor *accessors)
{
	int accessorIndex = 0;
	for (int i = 0; i < tokens->size; i++)
	{
		jsmntok_t *token = &tokens[i];

		if (token->type != JSMN_OBJECT)
		{
			continue;
		}

		GltfAccessor tempAccessor = {0};

		for (int j = 0; j < token->size; j++)
		{
			int key_token_index = i * 2 + 1;
			jsmntok_t key_token = tokens[key_token_index];
			int value_token_index = key_token_index + 1;
			jsmntok_t value_token = tokens[value_token_index];

			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch(hash)
			{
				case GLTF_BUFFERVIEW:
					tempAccessor.bufferView = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				case GLTF_COMPONENTTYPE:
					tempAccessor.componentType = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				case GLTF_COUNT:
					tempAccessor.count = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				case GLTF_MAX:
					parseAccessorMaxMin(json, &tokens[value_token_index + 1], tempAccessor.max, value_token.size);
					break;

				case GLTF_MIN:
					parseAccessorMaxMin(json, &tokens[value_token_index + 1], tempAccessor.min, value_token.size);
					break;

				case GLTF_TYPE:
					strncpy(tempAccessor.type, json + value_token.start, value_token.end - value_token.start);
					tempAccessor.type[value_token.end - value_token.start] = '\0';
					break;

				default:
					break;
			}
		}

		accessors[accessorIndex++] = tempAccessor;
	}
}

void helperParseBufferViews(const char *json, jsmntok_t *tokens, GltfBufferView *bufferViews)
{
	int bufferViewIndex = 0;
	for (int i = 0; i < tokens->size; i++)
	{
		jsmntok_t *token = &tokens[i];

		// Check if the token is of type JSMN_OBJECT
		if (token->type != JSMN_OBJECT)
		{
			continue;
		}

		// Initialize the GltfBufferView
		GltfBufferView tempBufferView = {0};

		for (int j = 0; j < token->size; j++)
		{
			int key_token_index = i * 2 + 1;
			jsmntok_t key_token = tokens[key_token_index];
			int value_token_index = key_token_index + 1;
			jsmntok_t value_token = tokens[value_token_index];

			// Extract and hash the key string
			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch(hash)
			{
				case GLTF_BUFFER:
					tempBufferView.index = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				case GLTF_BYTELENGTH:
					tempBufferView.byteLength = (uint64_t)strtoll(json + value_token.start, NULL, 10);
					break;

				case GLTF_BYTEOFFSET:
					tempBufferView.byteOffset = (uint64_t)strtoll(json + value_token.start, NULL, 10);
					break;

				default:
					// Handle unknown or unhandled keys
					break;
			}
		}

		bufferViews[bufferViewIndex++] = tempBufferView;
	}
}

void helperParseSamplers(const char *json, jsmntok_t *tokens, GltfSampler *samplers)
{
	int samplerIndex = 0;
	for (int i = 0; i < tokens->size; i++)
	{
		jsmntok_t *token = &tokens[i];

		// Check if the token is of type JSMN_OBJECT
		if (token->type != JSMN_OBJECT)
		{
			continue;
		}

		// Initialize the GltfSampler
		GltfSampler tempSampler = {0, 0};

		for (int j = 0; j < token->size; j++)
		{
			int key_token_index = i * 2 + 1;
			jsmntok_t key_token = tokens[key_token_index];
			int value_token_index = key_token_index + 1;
			jsmntok_t value_token = tokens[value_token_index];

			// Extract and hash the key string
			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch(hash)
			{
				case GLTF_MAGFILTER:
					tempSampler.magFilter = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				case GLTF_MINFILTER:
					tempSampler.minFilter = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				default:
					// Handle unknown or unhandled keys
					break;
			}
		}

		samplers[samplerIndex++] = tempSampler;
	}
}

void helperParseBuffers(const char *json, jsmntok_t *tokens, GltfBuffer *buffers)
{
	int bufferIndex = 0;
	for (int i = 0; i < tokens->size; i++)
	{
		jsmntok_t *token = &tokens[i];

		// Check if the token is of type JSMN_OBJECT
		if (token->type != JSMN_OBJECT)
		{
			continue;
		}

		// Initialize the GltfBuffer
		GltfBuffer tempBuffer = {0, 0, NULL, NULL};

		for (int j = 0; j < token->size; j++)
		{
			int key_token_index = i * 2 + 1;
			jsmntok_t key_token = tokens[key_token_index];
			int value_token_index = key_token_index + 1;
			jsmntok_t value_token = tokens[value_token_index];

			// Extract and hash the key string
			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch(hash)
			{
				case GLTF_BYTELENGTH:
					tempBuffer.byteLength = (uint64_t)strtoll(json + value_token.start, NULL, 10);
					break;

				case GLTF_URI:
					tempBuffer.uri = extractJsonString(json, &value_token);
					break;

				default:
					// Handle unknown or unhandled keys
					break;
			}
		}

		buffers[bufferIndex++] = tempBuffer;
	}
}

bool parseGltfElements(const char *json, GltfElements *elements)
{
	jsmn_parser parser;
	jsmntok_t *tokens;
	uint32_t tokenCount;

	// Initialize the parser
	jsmn_init(&parser);

	// Pass 1: Count the number of tokens
	tokenCount = jsmn_parse(&parser, json, strlen(json), NULL, 0);

	// Check for successful token count
	if (tokenCount <= 0)
	{
		return false;  // Parsing failed.
	}

	// Allocate memory for the tokens
	tokens = (jsmntok_t *)malloc(sizeof(jsmntok_t) * tokenCount);

	// Pass 2: Parse the JSON and fill the token array
	tokenCount = jsmn_parse(&parser, json, strlen(json), tokens, tokenCount);

	// Iterate over all tokens, increment counts and call helper functions
	for (int i = 0; i < tokenCount; i++)
	{
		if (tokens[i].type == JSMN_STRING)
		{
			// Create a temporary string for the key
			int keyLength = tokens[i].end - tokens[i].start;
			char key[keyLength + 1];
			memcpy(key, &json[tokens[i].start], keyLength);
			key[keyLength] = '\0';

			uint32_t hash = keyHash(key);

			// Use switch statement for hashed key values
			switch(hash)
			{
				case GLTF_SCENES:
					// The next token should be the object/array associated with "scenes"
					if (i + 1 < tokenCount && tokens[i + 1].type == JSMN_ARRAY)
					{
						helperParseScenes(json, &tokens[i + 1], elements->scenes);
						i++;
					}
					break;
				case GLTF_NODES:
					// The next token should be the object/array associated with "scenes"
					if (i + 1 < tokenCount && tokens[i + 1].type == JSMN_ARRAY)
					{
						helperParseNodes(json, &tokens[i + 1], elements->nodes);
						i++;
					}
					break;
				case GLTF_MATERIALS:
					// The next token should be the object/array associated with "scenes"
					if (i + 1 < tokenCount && tokens[i + 1].type == JSMN_ARRAY)
					{
						helperParseMaterials(json, &tokens[i + 1], elements->materials);
						i++;
					}
					break;
				case GLTF_MESHES:
					// The next token should be the object/array associated with "scenes"
					if (i + 1 < tokenCount && tokens[i + 1].type == JSMN_ARRAY)
					{
						helperParseMeshes(json, &tokens[i + 1], elements->meshes);
						i++;
					}
					break;
				case GLTF_TEXTURES:
					// The next token should be the object/array associated with "scenes"
					if (i + 1 < tokenCount && tokens[i + 1].type == JSMN_ARRAY)
					{
						helperParseTextures(json, &tokens[i + 1], elements->textures);
						i++;
					}
					break;
				case GLTF_IMAGES:
					// The next token should be the object/array associated with "scenes"
					if (i + 1 < tokenCount && tokens[i + 1].type == JSMN_ARRAY)
					{
						helperParseImages(json, &tokens[i + 1], elements->images);
						i++;
					}
					break;
				case GLTF_ACCESSORS:
					// The next token should be the object/array associated with "scenes"
					if (i + 1 < tokenCount && tokens[i + 1].type == JSMN_ARRAY)
					{
						helperParseAccessors(json, &tokens[i + 1], elements->accessors);
						i++;
					}
					break;
				case GLTF_BUFFERVIEWS:
					// The next token should be the object/array associated with "scenes"
					if (i + 1 < tokenCount && tokens[i + 1].type == JSMN_ARRAY)
					{
						helperParseBufferViews(json, &tokens[i + 1], elements->bufferViews);
						i++;
					}
					break;
				case GLTF_SAMPLERS:
					// The next token should be the object/array associated with "scenes"
					if (i + 1 < tokenCount && tokens[i + 1].type == JSMN_ARRAY)
					{
						helperParseSamplers(json, &tokens[i + 1], elements->samplers);
						i++;
					}
					break;
				case GLTF_BUFFERS:
					// The next token should be the object/array associated with "scenes"
					if (i + 1 < tokenCount && tokens[i + 1].type == JSMN_ARRAY)
					{
						helperParseBuffers(json, &tokens[i + 1], elements->buffers);
						i++;
					}
					break;
				default:
					// Handle unknown or unhandled keys
					break;
			}
		}
	}

	// Free the tokens array
	free(tokens);

	return true;
}

// The helper function definitions would go here
// Example:
// Helper function to parse a single scene element


bool createRenderableEntity()
{
	// Traverse element buffers from root entity to primitives, assigning pointers
	return true;
}

char* readFile(const char* filename, size_t* size)
{
	FILE* file;
	char* buffer;
	size_t fileSize;

	// Open the file
	file = fopen(filename, "rb"); // Open in binary mode for portability
	if (file == NULL)
	{
		printf("Error opening file: %s\n", filename);
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	fileSize = ftell(file);
	rewind(file);

	buffer = (char*)malloc(fileSize + 1);
	if (buffer == NULL)
	{
		printf("Error allocating memory for file: %s\n", filename);
		fclose(file);
		return NULL;
	}

	if (fread(buffer, 1, fileSize, file) != fileSize)
	{
		printf("Error reading file: %s\n", filename);
		free(buffer);
		fclose(file);
		return NULL;
	}

	buffer[fileSize] = '\0';

	fclose(file);

	if (size != NULL)
	{
		*size = fileSize;
	}

	return buffer;
}


bool parseGltf(const char* fileName)
{
	GltfElements elements;
	
	// Open file
	size_t fileSize;
	char* fileBuffer = readFile(fileName, &fileSize);
	if (fileBuffer != NULL)
	{
		if(!countGltfElements(fileBuffer, &elements))
		{
			printf("Error counting elements: %s\n", fileName);
			return false;
		}

		initializeGltfElements(&elements);
		parseGltfElements(fileBuffer, &elements);
	}

	// Record element counts
	// Allocate element buffers
	// Populate element buffers with parameters

	// Create renderable entity packages
	return true;
}
