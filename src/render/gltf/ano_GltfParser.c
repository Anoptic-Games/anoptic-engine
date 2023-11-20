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

char* extractJsonString(const char *json, jsmntok_t *token)
{
	int length = token->end - token->start;
	char *str = malloc(length + 1);
	memcpy(str, json + token->start, length);
	str[length] = '\0';
	return str;
}

// !TODO update every parsing function to use this for determining the actual number of tokens to be parsed
// !TODO step 2 will be to extend the logic in each of them to correctly step over nested objects of arbitrary size
int calculateTotalTokenSize(jsmntok_t *tokens, int tokenIndex)
{
	jsmntok_t token = tokens[tokenIndex];
	int totalSize = 0;

	for (int i = tokenIndex + 1; ; ++i)
	{
		if (tokens[i].end <= token.end)
		{
			totalSize++;
		} else
		{
			break; // Found a token outside the current token's range
		}
	}

	return totalSize + 1; // Include the current token itself
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
		printf("No tokens found!\n");
		return false;  // Parsing failed.
	}

	tokens = malloc(sizeof(jsmntok_t) * tokenCount);
	if (tokens == NULL)
	{
		printf("Failed to allocate token array!\n");
		return false;
	}
	jsmn_init(&parser);

	// Second pass to actually parse the JSON
	jsmn_parse(&parser, json, strlen(json), tokens, tokenCount);
	printf("TokenCount: %d!\n", tokenCount);
	for (int i = 0; i < tokenCount - 1; i++)  // -1 to prevent going out of bounds
	{
		//printf("Token type: %d", tokens[i].type);
		if (tokens[i].type == JSMN_STRING)
		{
			//printf("Should be parsing a string!\n");
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
	printf("Parsing scenes!\n");

	// The first token after the scenes array token is the first scene object
	int currentTokenIndex = 1; // Start from the first token after the scenes array token

	for (int sceneIndex = 0; sceneIndex < tokens[0].size; sceneIndex++)
	{
		jsmntok_t *sceneToken = &tokens[currentTokenIndex];

		// Check if the current token is a scene object
		if (sceneToken->type != JSMN_OBJECT)
		{
			// Handle error: not an object
			continue;
		}

		// Initialize the nodeCount for the current scene
		scenes[sceneIndex].nodeCount = 0;
		
		printf("Scene token size: %d\n", sceneToken->size);

		// Iterate over key-value pairs in the scene
		for (int pairIndex = 0; pairIndex < sceneToken->size; pairIndex++)
		{
			int key_token_index = currentTokenIndex + 1 + pairIndex * 2;
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
					printf("Read NAME\n");
					scenes[sceneIndex].name = extractJsonString(json, &value_token);
					break;
				case GLTF_NODES:
					printf("Read NODES\n");
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

		// Move to the next scene object token
		currentTokenIndex += 1 + sceneToken->size * 2; // Each key-value pair is 2 tokens, and there's an additional token for the scene object itself
	}
}

void helperParseNodes(const char *json, jsmntok_t *tokens, GltfNode *nodes)
{
	// Assuming the first token in tokens is the array of nodes
	int currentTokenIndex = 1; // Start from the first token after the nodes array token
	int name_length = 0;;

	for (int nodeIndex = 0; nodeIndex < tokens[0].size; nodeIndex++)
	{
		jsmntok_t *nodeToken = &tokens[currentTokenIndex];

		// Check if the current token is a node object
		if (nodeToken->type != JSMN_OBJECT)
		{
			continue; // Skip non-object tokens
		}

		// Initialize the GltfNode
		nodes[nodeIndex].mesh = 0;
		nodes[nodeIndex].name = NULL;
		nodes[nodeIndex].rotation = (Vector4){0, 0, 0, 0};

		// Iterate over key-value pairs in the node
		for (int pairIndex = 0; pairIndex < nodeToken->size; pairIndex++)
		{
			int key_token_index = currentTokenIndex + 1 + pairIndex * 2;
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
							int element_index = value_token_index + 1 + k;
							nodes[nodeIndex].rotation.v[k] = atof(json + tokens[element_index].start);
						}
					}
					break;

				default:
					// Handle unknown or unhandled keys
					break;
			}
		}

		// Move to the next node object token
		currentTokenIndex += 1 + nodeToken->size * 2; // Each key-value pair is 2 tokens, and there's an additional token for the node object itself
	}
}

void parsePbrMetallicRoughness(const char *json, jsmntok_t *tokens, PbrMetallicRoughness *pbr)
{
	int i = 1; // Start from the first token after the PBR object token
	int pbrEnd = i + tokens[0].size * 2; // Initially set end index for the PBR object

	while (i < pbrEnd)
	{
		jsmntok_t key_token = tokens[i];
		jsmntok_t value_token = tokens[i + 1];

		int key_length = key_token.end - key_token.start;
		char key[key_length + 1];
		memcpy(key, json + key_token.start, key_length);
		key[key_length] = '\0';
		uint32_t hash = keyHash(key);

		switch(hash)
		{
			case GLTF_BASECOLORTEXTURE:
				if (value_token.type == JSMN_OBJECT)
				{
					int baseTextureEnd = i + 2 + value_token.size * 2; // End index for the baseColorTexture object
					for (int j = i + 2; j < baseTextureEnd; j += 2)
					{
						jsmntok_t nestedKeyToken = tokens[j];
						jsmntok_t nestedValueToken = tokens[j + 1];
						// ... process nested object ...
					}
					i = baseTextureEnd; // Move to the end of the baseColorTexture object
					pbrEnd += value_token.size * 2; // Update pbrEnd to account for nested object size
				} else
				{
					i += 2; // Simple key-value pair
				}
				break;

				case GLTF_METALLICFACTOR:
					//printf("METALIC FACTOR!!!!\n");
					pbr->metallicFactor = atof(json + value_token.start);
					i += 2; // Move to the next key-value pair
					//printf("Current index: %d\n", i);
					break;

				case GLTF_ROUGHNESSFACTOR:
					//printf("ROUGHNESS FACTOR!!!!\n");
					pbr->roughnessFactor = atof(json + value_token.start);
					i += 2; // Move to the next key-value pair
					break;

				default:
					i += 2; // Skip unknown properties
					break;
		}
	}
}

void helperParseMaterials(const char *json, jsmntok_t *tokens, GltfMaterial *materials)
{
	int currentTokenIndex = 1; // Start from the first token after the materials array token
	int name_length = 0;

	for (int materialIndex = 0; materialIndex < tokens[0].size; materialIndex++)
	{
		jsmntok_t *materialToken = &tokens[currentTokenIndex];

		if (materialToken->type != JSMN_OBJECT)
		{
			continue;
		}

		materials[materialIndex].doubleSided = false;
		materials[materialIndex].name = NULL;
		materials[materialIndex].pbr = (PbrMetallicRoughness){0, 0.0f, 0.0f};

		for (int pairIndex = 0; pairIndex < materialToken->size; pairIndex++)
		{
			int key_token_index = currentTokenIndex + 1 + pairIndex * 2;
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
				case GLTF_DOUBLESIDED:
					materials[materialIndex].doubleSided = (strncmp(json + value_token.start, "true", 4) == 0);
					break;

				case GLTF_NAME:
					name_length = value_token.end - value_token.start;
					materials[materialIndex].name = malloc(name_length + 1);
					memcpy(materials[materialIndex].name, json + value_token.start, name_length);
					materials[materialIndex].name[name_length] = '\0';
					break;

				case GLTF_PBRMETALICROUGHNESS:
					if (value_token.type == JSMN_OBJECT)
					{
						jsmntok_t *pbrTokens = &tokens[value_token_index];
						parsePbrMetallicRoughness(json, pbrTokens, &materials[materialIndex].pbr);
					}
					break;

				default:
					break;
			}
		}

		currentTokenIndex += 1 + materialToken->size * 2; // Move to the next material object token
	}
}

void parsePrimitive(const char *json, jsmntok_t *tokens, GltfPrimitive *primitive)
{
	int end = tokens[0].size * 2 + 1; // Calculate the end index for the Primitive object
	//printf("THE END IS: %d\n", end);
	for (int i = 1; i < end; i += 2) { // Process each key-value pair within the Primitive object
		jsmntok_t key_token = tokens[i];
		jsmntok_t value_token = tokens[i + 1];

		int key_length = key_token.end - key_token.start;
		char key[key_length + 1];
		memcpy(key, json + key_token.start, key_length);
		key[key_length] = '\0';
		uint32_t hash = keyHash(key);

		if (strncmp(key, "attributes", key_length) == 0 && value_token.type == JSMN_OBJECT)
		{
			// Process the nested attributes object
			int attributesEnd = i + 2 + value_token.size * 2; // End index for the attributes object
			for (int j = i + 2; j < attributesEnd; j += 2)
			{
				jsmntok_t nestedKeyToken = tokens[j];
				jsmntok_t nestedValueToken = tokens[j + 1];

				int nestedKeyLength = nestedKeyToken.end - nestedKeyToken.start;
				char nestedKey[nestedKeyLength + 1];
				memcpy(nestedKey, json + nestedKeyToken.start, nestedKeyLength);
				nestedKey[nestedKeyLength] = '\0';
				uint32_t nestedHash = keyHash(nestedKey);

				uint32_t value = (uint32_t)strtol(json + nestedValueToken.start, NULL, 10);

				switch (nestedHash)
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
					default:
						break;
				}
			}
			end = end + 6;
			i = attributesEnd - 2; // Correctly position i to the token after the attributes object
		} else
		{
			// Process non-attributes keys
			uint32_t value = (uint32_t)strtol(json + value_token.start, NULL, 10);
			//printf("Index value on non-attributes read: %d\n", i);
			switch (hash)
			{
				case GLTF_INDICES:
					//printf("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
					primitive->indices = value;
					break;
				case GLTF_MATERIAL:
					//printf("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
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
	int currentTokenIndex = 1; // Start from the first token after the meshes array token
	int primitivesEnd = 0;
	int name_length = 0;
	for (int meshIndex = 0; meshIndex < tokens[0].size; meshIndex++)
	{
		jsmntok_t *meshToken = &tokens[currentTokenIndex];

		if (meshToken->type != JSMN_OBJECT)
		{
			continue;
		}

		meshes[meshIndex].name = NULL;
		memset(&meshes[meshIndex].primitives, 0, sizeof(GltfPrimitive));

		int meshTokenEnd = currentTokenIndex + 1 + meshToken->size * 2;

		for (int i = currentTokenIndex + 1; i < meshTokenEnd; i += 2)
		{
			jsmntok_t key_token = tokens[i];
			jsmntok_t value_token = tokens[i + 1];

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
						primitivesEnd = i + 1 + value_token.size * 2; // Calculate end index for the primitives array
						for (int j = i + 2; j < primitivesEnd; j += 2)
						{
							jsmntok_t *primitiveToken = &tokens[j];
							parsePrimitive(json, primitiveToken, &meshes[meshIndex].primitives);
						}
						i = primitivesEnd; // Skip past the primitives array
					}
					break;

				default:
					break;
			}
		}

		currentTokenIndex = meshTokenEnd; // Move to the next mesh object token
	}
}

void helperParseTextures(const char *json, jsmntok_t *tokens, GltfTexture *textures)
{
	int currentTokenIndex = 1; // Start from the first token after the textures array token

	for (int textureIndex = 0; textureIndex < tokens[0].size; textureIndex++)
	{
		jsmntok_t *textureToken = &tokens[currentTokenIndex];

		if (textureToken->type != JSMN_OBJECT)
		{
			continue;
		}

		textures[textureIndex].sampler = 0;
		textures[textureIndex].source = 0;

		int textureTokenEnd = currentTokenIndex + 1 + textureToken->size * 2;

		for (int i = currentTokenIndex + 1; i < textureTokenEnd; i += 2)
		{
			jsmntok_t key_token = tokens[i];
			jsmntok_t value_token = tokens[i + 1];

			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch (hash)
			{
				case GLTF_SAMPLER:
					textures[textureIndex].sampler = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				case GLTF_SOURCE:
					textures[textureIndex].source = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				default:
					break;
			}
		}

		currentTokenIndex = textureTokenEnd; // Move to the next texture object token
	}
}

void helperParseImages(const char *json, jsmntok_t *tokens, GltfImage *images)
{
	int currentTokenIndex = 1; // Start from the first token after the images array token

	for (int imageIndex = 0; imageIndex < tokens[0].size; imageIndex++)
	{
		jsmntok_t *imageToken = &tokens[currentTokenIndex];

		if (imageToken->type != JSMN_OBJECT)
		{
			continue;
		}

		images[imageIndex].mimeType = NULL;
		images[imageIndex].name = NULL;
		images[imageIndex].uri = NULL;

		int imageTokenEnd = currentTokenIndex + 1 + imageToken->size * 2;

		for (int i = currentTokenIndex + 1; i < imageTokenEnd; i += 2)
		{
			jsmntok_t key_token = tokens[i];
			jsmntok_t value_token = tokens[i + 1];

			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch (hash)
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
					break;
			}
		}

		currentTokenIndex = imageTokenEnd; // Move to the next image object token
	}
}
void parseAccessorMaxMin(const char *json, jsmntok_t *tokens, float *array, int size)
{
	//printf("Array size: %d\n", size);
	for (int i = 0; i < size; i++)
	{
		array[i] = atof(json + tokens[i].start);
		//printf("Extracted value: array[%d] = %f", i, array[i]);
	}
}

void helperParseAccessors(const char *json, jsmntok_t *tokens, GltfAccessor *accessors)
{
	int currentTokenIndex = 1; // Start from the first token after the accessors array token

	for (int accessorIndex = 0; accessorIndex < tokens[0].size; accessorIndex++)
	{
		jsmntok_t *accessorToken = &tokens[currentTokenIndex];

		if (accessorToken->type != JSMN_OBJECT)
		{
			continue;
		}

		GltfAccessor tempAccessor = {0};
		int actualSize = calculateTotalTokenSize(&tokens[currentTokenIndex], 0);

		int accessorTokenEnd = currentTokenIndex + actualSize;
		printf("Tokens to be parsed: %d\n", accessorTokenEnd);
		for (int i = currentTokenIndex + 1; i < accessorTokenEnd; i += 2)
		{
			jsmntok_t key_token = tokens[i];
			jsmntok_t value_token = tokens[i + 1];

			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);
			printf("Current index: %d\n", i);
			switch (hash)
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
					if (value_token.type == JSMN_ARRAY)
					{
						parseAccessorMaxMin(json, &tokens[i + 2], tempAccessor.max, value_token.size);
						i += value_token.size; // Correctly position i after the max array
					}
					break;

				case GLTF_MIN:
					if (value_token.type == JSMN_ARRAY)
					{
						parseAccessorMaxMin(json, &tokens[i + 2], tempAccessor.min, value_token.size);
						i += value_token.size; // Correctly position i after the min array
					}
					break;

				case GLTF_TYPE:
					strncpy(tempAccessor.type, json + value_token.start, value_token.end - value_token.start);
					tempAccessor.type[value_token.end - value_token.start] = '\0';
					break;

				default:
					break;
			}
		}

		accessors[accessorIndex] = tempAccessor;
		currentTokenIndex = accessorTokenEnd; // Move to the next accessor object token
	}
}

void helperParseBufferViews(const char *json, jsmntok_t *tokens, GltfBufferView *bufferViews)
{
	int currentTokenIndex = 1; // Start from the first token after the bufferViews array token

	for (int bufferViewIndex = 0; bufferViewIndex < tokens[0].size; bufferViewIndex++)
	{
		jsmntok_t *bufferViewToken = &tokens[currentTokenIndex];

		if (bufferViewToken->type != JSMN_OBJECT)
		{
			continue;
		}

		GltfBufferView tempBufferView = {0};

		int bufferViewTokenEnd = currentTokenIndex + 1 + bufferViewToken->size * 2;

		for (int i = currentTokenIndex + 1; i < bufferViewTokenEnd; i += 2)
		{
			jsmntok_t key_token = tokens[i];
			jsmntok_t value_token = tokens[i + 1];

			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch (hash)
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
					break;
			}
		}

		bufferViews[bufferViewIndex] = tempBufferView;
		currentTokenIndex = bufferViewTokenEnd; // Move to the next bufferView object token
	}
}

void helperParseSamplers(const char *json, jsmntok_t *tokens, GltfSampler *samplers)
{
	int currentTokenIndex = 1; // Start from the first token after the samplers array token

	for (int samplerIndex = 0; samplerIndex < tokens[0].size; samplerIndex++)
	{
		jsmntok_t *samplerToken = &tokens[currentTokenIndex];

		if (samplerToken->type != JSMN_OBJECT)
		{
			continue;
		}

		GltfSampler tempSampler = {0, 0};

		int samplerTokenEnd = currentTokenIndex + 1 + samplerToken->size * 2;

		for (int i = currentTokenIndex + 1; i < samplerTokenEnd; i += 2)
		{
			jsmntok_t key_token = tokens[i];
			jsmntok_t value_token = tokens[i + 1];

			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch (hash)
			{
				case GLTF_MAGFILTER:
					tempSampler.magFilter = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				case GLTF_MINFILTER:
					tempSampler.minFilter = (uint32_t)strtol(json + value_token.start, NULL, 10);
					break;

				default:
					break;
			}
		}

		samplers[samplerIndex] = tempSampler;
		currentTokenIndex = samplerTokenEnd; // Move to the next sampler object token
	}
}

void helperParseBuffers(const char *json, jsmntok_t *tokens, GltfBuffer *buffers)
{
	int currentTokenIndex = 1; // Start from the first token after the buffers array token

	for (int bufferIndex = 0; bufferIndex < tokens[0].size; bufferIndex++)
	{
		jsmntok_t *bufferToken = &tokens[currentTokenIndex];

		if (bufferToken->type != JSMN_OBJECT)
		{
			continue;
		}

		GltfBuffer tempBuffer = {0, 0, NULL, NULL};

		int bufferTokenEnd = currentTokenIndex + 1 + bufferToken->size * 2;

		for (int i = currentTokenIndex + 1; i < bufferTokenEnd; i += 2)
		{
			jsmntok_t key_token = tokens[i];
			jsmntok_t value_token = tokens[i + 1];

			int key_length = key_token.end - key_token.start;
			char key[key_length + 1];
			memcpy(key, json + key_token.start, key_length);
			key[key_length] = '\0';
			uint32_t hash = keyHash(key);

			switch (hash)
			{
				case GLTF_BYTELENGTH:
					tempBuffer.byteLength = (uint64_t)strtoll(json + value_token.start, NULL, 10);
					break;

				case GLTF_URI:
					tempBuffer.uri = extractJsonString(json, &value_token);
					break;

				default:
					break;
			}
		}

		buffers[bufferIndex] = tempBuffer;
		currentTokenIndex = bufferTokenEnd; // Move to the next buffer object token
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
	printf("Second pass tokenCount: %d\n", tokenCount);
	jsmn_init(&parser);
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

// Debug printout functions

void printGltfScenes(const GltfElements* elements)
{
	if (elements == NULL)
	{
		printf("GltfElements is NULL.\n");
		return;
	}

	printf("Total scenes: %u\n", elements->sceneCount);
	for (uint32_t i = 0; i < elements->sceneCount; i++)
	{
		GltfScene scene = elements->scenes[i];
		printf("Scene %u:\n", i);
		printf("  Name: %s\n", scene.name);
		printf("  Node Count: %u\n", scene.nodeCount);

		printf("  Nodes: [");
		for (uint32_t j = 0; j < scene.nodeCount; j++)
		{
			printf("%u", scene.nodes[j]);
			if (j < scene.nodeCount - 1)
			{
				printf(", ");
			}
		}
		printf("]\n");
	}
	printf("================\n");
}

void printGltfNodes(const GltfElements* elements)
{
	if (elements == NULL)
	{
		printf("GltfElements is NULL.\n");
		return;
	}

	printf("Total nodes: %u\n", elements->nodeCount);
	for (uint32_t i = 0; i < elements->nodeCount; i++)
	{
		GltfNode node = elements->nodes[i];
		printf("Node %u:\n", i);
		printf("  Name: %s\n", node.name);
		printf("  Mesh: %u\n", node.mesh);
		printf("  Rotation: (%f, %f, %f, %f)\n", node.rotation.v[0], node.rotation.v[1], node.rotation.v[2], node.rotation.v[3]);
	}
	printf("================\n");
}

void printGltfMaterials(const GltfElements* elements)
{
	if (elements == NULL)
	{
		printf("GltfElements is NULL.\n");
		return;
	}

	printf("Total materials: %u\n", elements->materialCount);
	for (uint32_t i = 0; i < elements->materialCount; i++)
	{
		GltfMaterial material = elements->materials[i];
		printf("Material %u:\n", i);
		printf("  Name: %s\n", material.name);
		printf("  Double Sided: %s\n", material.doubleSided ? "True" : "False");
		printf("  PBR Properties:\n");
		printf("	Base Color Texture: %u\n", material.pbr.baseColorTexture);
		printf("	Metallic Factor: %f\n", material.pbr.metallicFactor);
		printf("	Roughness Factor: %f\n", material.pbr.roughnessFactor);
	}
	printf("================\n");
}

void printGltfPrimitive(const GltfPrimitive* primitive)
{
	if (primitive == NULL)
	{
		printf("GltfPrimitive is NULL.\n");
		return;
	}

	printf("  Primitive:\n");
	printf("	Position: %u\n", primitive->position);
	printf("	Normal: %u\n", primitive->normal);
	printf("	Texcoord: %u\n", primitive->texcoord);
	printf("	Indices: %u\n", primitive->indices);
	printf("	Material: %u\n", primitive->material);
}

void printGltfMeshes(const GltfElements* elements)
{
	if (elements == NULL)
	{
		printf("GltfElements is NULL.\n");
		return;
	}

	printf("Total meshes: %u\n", elements->meshCount);
	for (uint32_t i = 0; i < elements->meshCount; i++)
	{
		GltfMesh mesh = elements->meshes[i];
		printf("Mesh %u:\n", i);
		printf("  Name: %s\n", mesh.name);
		printGltfPrimitive(&mesh.primitives);
	}
	printf("================\n");
}

void printGltfImages(const GltfElements* elements)
{
	if (elements == NULL)
	{
		printf("GltfElements is NULL.\n");
		return;
	}

	printf("Total images: %u\n", elements->imageCount);
	for (uint32_t i = 0; i < elements->imageCount; i++)
	{
		GltfImage image = elements->images[i];
		printf("Image %u:\n", i);
		printf("  Name: %s\n", image.name);
		printf("  MIME Type: %s\n", image.mimeType);
		printf("  URI: %s\n", image.uri);
	}
	printf("================\n");
}

void printGltfTextures(const GltfElements* elements)
{
	if (elements == NULL)
	{
		printf("GltfElements is NULL.\n");
		return;
	}

	printf("Total textures: %u\n", elements->textureCount);
	for (uint32_t i = 0; i < elements->textureCount; i++)
	{
		GltfTexture texture = elements->textures[i];
		printf("Texture %u:\n", i);
		printf("  Sampler: %u\n", texture.sampler);
		printf("  Source Image Index: %u\n", texture.source);
	}
	printf("================\n");
}

int getComponentCount(const char* type)
{
	if (strcmp(type, "SCALAR") == 0) return 1;
	if (strcmp(type, "VEC2") == 0) return 2;
	if (strcmp(type, "VEC3") == 0) return 3;
	if (strcmp(type, "VEC4") == 0) return 4;
	if (strcmp(type, "MAT2") == 0) return 4; // 2x2 matrix
	if (strcmp(type, "MAT3") == 0) return 9; // 3x3 matrix
	if (strcmp(type, "MAT4") == 0) return 16; // 4x4 matrix
	return 0; // Unknown type
}

void printGltfAccessors(const GltfElements* elements)
{
	if (elements == NULL)
	{
		printf("GltfElements is NULL.\n");
		return;
	}

	printf("Total accessors: %u\n", elements->accessorCount);
	for (uint32_t i = 0; i < elements->accessorCount; i++)
	{
		GltfAccessor accessor = elements->accessors[i];
		int componentCount = getComponentCount(accessor.type);

		printf("Accessor %u:\n", i);
		printf("  Buffer View: %u\n", accessor.bufferView);
		printf("  Component Type: %u\n", accessor.componentType);
		printf("  Count: %u\n", accessor.count);
		printf("  Type: %s\n", accessor.type);

		printf("  Max: [");
		for (int j = 0; j < componentCount && j < 4; j++)
		{
			printf("%f ", accessor.max[j]);
		}
		printf("]\n");

		printf("  Min: [");
		for (int j = 0; j < componentCount && j < 4; j++)
		{
			printf("%f ", accessor.min[j]);
		}
		printf("]\n");
	}
	printf("================\n");
}

void printGltfBufferViews(const GltfElements* elements)
{
	if (elements == NULL)
	{
		printf("GltfElements is NULL.\n");
		return;
	}

	printf("Total buffer views: %u\n", elements->bufferViewCount);
	for (uint32_t i = 0; i < elements->bufferViewCount; i++)
	{
		GltfBufferView bufferView = elements->bufferViews[i];
		printf("Buffer View %u:\n", i);
		printf("  Index: %u\n", bufferView.index);
		printf("  Byte Length: %llu\n", bufferView.byteLength);
		printf("  Byte Offset: %llu\n", bufferView.byteOffset);
	}
	printf("================\n");
}

void printGltfSamplers(const GltfElements* elements)
{
	if (elements == NULL)
	{
		printf("GltfElements is NULL.\n");
		return;
	}

	printf("Total samplers: %u\n", elements->samplerCount);
	for (uint32_t i = 0; i < elements->samplerCount; i++)
	{
		GltfSampler sampler = elements->samplers[i];
		printf("Sampler %u:\n", i);
		printf("  Magnification Filter: %u\n", sampler.magFilter);
		printf("  Minification Filter: %u\n", sampler.minFilter);
	}
	printf("================\n");
}

void printGltfBuffers(const GltfElements* elements)
{
	if (elements == NULL)
	{
		printf("GltfElements is NULL.\n");
		return;
	}

	printf("Total buffers: %u\n", elements->bufferCount);
	for (uint32_t i = 0; i < elements->bufferCount; i++)
	{
		GltfBuffer buffer = elements->buffers[i];
		printf("Buffer %u:\n", i);
		printf("  Index: %u\n", buffer.index);
		printf("  Byte Length: %llu\n", buffer.byteLength);
		printf("  Address: %p\n", buffer.address);
		printf("  URI: %s\n", buffer.uri ? buffer.uri : "NULL");
	}
	printf("================\n");
}

void printGltfElementsContents(const GltfElements* elements)
{
	if (elements == NULL)
	{
		printf("GltfElements is NULL.\n");
		return;
	}

	printGltfScenes(elements);
	printGltfNodes(elements);
	printGltfMaterials(elements);
	printGltfMeshes(elements);
	printGltfImages(elements);
	printGltfTextures(elements);
	printGltfAccessors(elements);
	printGltfBufferViews(elements);
	printGltfSamplers(elements);
	printGltfBuffers(elements);
}


//Tie everything together

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
	} else
	{
		printf("Failed to read glTF file: %s", fileName);
		return false;
	}
	//printf("Scenes count: %d\n", elements.sceneCount);
	printGltfElementsContents(&elements);

	// Record element counts
	// Allocate element buffers
	// Populate element buffers with parameters

	// Create renderable entity packages
	return true;
}
