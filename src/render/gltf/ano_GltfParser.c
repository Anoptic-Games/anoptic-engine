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
uint32_t calculateTotalTokenSize(jsmntok_t *tokens, int tokenIndex)
{
	jsmntok_t token = tokens[tokenIndex];
	uint32_t totalSize = 0;

	for (int i = tokenIndex + 1; ; ++i)
	{
		if(tokens[i].type == JSMN_UNDEFINED && tokens[i].end == 0) // Reached the sentinel token
		{
			break;
		}
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

// Tests whether a given character occurs within the given bounds of a string
int charExistsBetween(const char arr[], int start, int end, char target)
{
	// Validate indices
	if (start < 0 || end < 0 || start > end)
	{
		printf("Invalid indices.\n");
		return -1;
	}

	// Search for the character between start and end
	for (int i = start; i <= end; i++)
	{
		if (arr[i] == target)
		{
			return 1;  // Character found
		}
	}

	return 0;  // Character not found
}

// Returns a reliable offset by which to increment token arrays during parsing
uint32_t getIncrement(const char *json, jsmntok_t *token)
{
	uint32_t valueSize = 0;
	int pairStatus = charExistsBetween(json, token[0].end, token[1].start, ':'); // Key-value pair test
	if (pairStatus == 1) 
	{
		// We'll ignore the very concept of a multitude being used as a key
		valueSize = calculateTotalTokenSize(&token[1], 0);
	} else if (pairStatus == -1)
	{
		printf("Invalid token pair check! NEAR: %d, FAR:%d\n", token[0].end, token[1].start);
		return 0;
	}
	return valueSize + calculateTotalTokenSize(&token[0], 0); // Add 1 for the key token
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

void freeGltfElements(GltfElements *elements)
{
    if (elements->scenes)
    {
        free(elements->scenes);
    }
    if (elements->nodes)
    {
        free(elements->nodes);
    }
    if (elements->materials)
    {
        free(elements->materials);
    }
    if (elements->meshes)
    {
        free(elements->meshes);
    }
    if (elements->textures)
    {
        free(elements->textures);
    }
    if (elements->images)
    {
        free(elements->images);
    }
    if (elements->accessors)
    {
        free(elements->accessors);
    }
    if (elements->bufferViews)
    {
        free(elements->bufferViews);
    }
    if (elements->samplers)
    {
        free(elements->samplers);
    }
    if (elements->buffers)
    {
        free(elements->buffers);
    }
}

void helperParseScenes(const char *json, jsmntok_t *tokens, GltfScene *scenes)
{
	printf("Parsing scenes!\n");

	// The first token after the scenes array token is the first scene object
	int currentTokenIndex = 1; // Start from the first token after the scenes array token

	for (int sceneIndex = 0; sceneIndex < tokens[0].size; sceneIndex++)
	{
		jsmntok_t *sceneToken = &tokens[currentTokenIndex];

		if (sceneToken->type != JSMN_OBJECT)
		{
			// Handle error: not an object
			continue;
		}

		scenes[sceneIndex].nodeCount = 0;
		scenes[sceneIndex].name = "NONE";
		printf("Scene token size: %d\n", sceneToken->size);

		int pairIndex = 0;
		int keyTokenIndex = currentTokenIndex + 1;

		while (pairIndex < sceneToken->size)
		{
			jsmntok_t keyToken = tokens[keyTokenIndex];
			jsmntok_t valueToken = tokens[keyTokenIndex + 1];

			int keyLength = keyToken.end - keyToken.start;
			char key[keyLength + 1];
			memcpy(key, json + keyToken.start, keyLength);
			key[keyLength] = '\0';

			uint32_t hash = keyHash(key);

			switch(hash)
			{
				case GLTF_NAME:
					printf("Read NAME\n");
					scenes[sceneIndex].name = extractJsonString(json, &valueToken);
					break;
				case GLTF_NODES:
					printf("Read NODES\n");
					if (valueToken.type == JSMN_ARRAY)
					{
						scenes[sceneIndex].nodeCount = valueToken.size;
						scenes[sceneIndex].nodes = malloc(sizeof(uint32_t) * valueToken.size);

						for (int k = 0; k < valueToken.size; k++)
						{
							int nodeIndex = keyTokenIndex + 2 + k;
							int nodeValueLength = tokens[nodeIndex].end - tokens[nodeIndex].start;
							char nodeValueStr[nodeValueLength + 1];
							memcpy(nodeValueStr, json + tokens[nodeIndex].start, nodeValueLength);
							nodeValueStr[nodeValueLength] = '\0';

							scenes[sceneIndex].nodes[k] = (uint32_t)strtol(nodeValueStr, NULL, 10);
						}
					}
					break;
				// Add more cases for other keys if needed
				default:
					break;
			}

			// Update indices for next iteration
			pairIndex++;
			uint32_t keyIncrement = getIncrement(json, &tokens[keyTokenIndex]);
			keyTokenIndex += keyIncrement;
			if (keyIncrement == 0)
			{
				printf("FATAL ERROR: key-value token mismatch, scene#%d!\n", sceneIndex);
				exit(-100);
			}
		}

		// Move to the next scene object token
		currentTokenIndex += calculateTotalTokenSize(tokens, currentTokenIndex);
	}
}

void helperParseNodes(const char *json, jsmntok_t *tokens, GltfNode *nodes)
{
	int currentTokenIndex = 1; // Start from the first token after the nodes array token
	int nameLength = 0;
	int elementIndex = 0;

	for (int nodeIndex = 0; nodeIndex < tokens[0].size; nodeIndex++)
	{
		jsmntok_t *nodeToken = &tokens[currentTokenIndex];
		uint32_t nodeSize = calculateTotalTokenSize(tokens, currentTokenIndex);

		if (nodeToken->type != JSMN_OBJECT)
		{
			continue; // Skip non-object tokens
		}

		nodes[nodeIndex].mesh = 0;
		nodes[nodeIndex].name = NULL;
		nodes[nodeIndex].rotation = (Vector4){0, 0, 0, 0};

		int pairIndex = 0;
		int keyTokenIndex = currentTokenIndex + 1;

		while (pairIndex < nodeToken->size)
		{
			jsmntok_t keyToken = tokens[keyTokenIndex];
			jsmntok_t valueToken = tokens[keyTokenIndex + 1];

			int keyLength = keyToken.end - keyToken.start;
			char key[keyLength + 1];
			memcpy(key, json + keyToken.start, keyLength);
			key[keyLength] = '\0';

			uint32_t hash = keyHash(key);

			switch(hash)
			{
				case GLTF_MESH:
					nodes[nodeIndex].mesh = (uint32_t)strtol(json + valueToken.start, NULL, 10);
					break;

				case GLTF_NAME:
					nameLength = valueToken.end - valueToken.start;
					nodes[nodeIndex].name = malloc(nameLength + 1);
					memcpy(nodes[nodeIndex].name, json + valueToken.start, nameLength);
					nodes[nodeIndex].name[nameLength] = '\0';
					break;

				case GLTF_ROTATION:
					if (valueToken.type == JSMN_ARRAY)
					{
						for (int k = 0; k < valueToken.size; k++)
						{
							elementIndex = keyTokenIndex + 2 + k;
							nodes[nodeIndex].rotation.v[k] = atof(json + tokens[elementIndex].start);
						}
					}
					break;

				default:
					break;
			}

			// Update indices for next iteration
			pairIndex++;
			uint32_t keyIncrement = getIncrement(json, &tokens[keyTokenIndex]);
			keyTokenIndex += keyIncrement;
			if (keyIncrement == 0)
			{
				printf("FATAL ERROR: key-value token mismatch, node#%d!\n", nodeIndex);
				exit(-100);
			}
		}

		// Move to the next node object token
		currentTokenIndex += nodeSize;
	}
}

void parsePbrMetallicRoughness(const char *json, jsmntok_t *tokens, PbrMetallicRoughness *pbr)
{
	int i = 1; // Start from the first token after the PBR object token
	int pbrEnd = i + calculateTotalTokenSize(tokens, i);

	uint32_t keyIncrement;

	while (i < pbrEnd)
	{
		jsmntok_t keyToken = tokens[i];
		jsmntok_t valueToken = tokens[i + 1];

		int keyLength = keyToken.end - keyToken.start;
		char key[keyLength + 1];
		memcpy(key, json + keyToken.start, keyLength);
		key[keyLength] = '\0';
		uint32_t hash = keyHash(key);

		switch(hash)
		{
			case GLTF_BASECOLORTEXTURE:
				if (valueToken.type == JSMN_OBJECT)
				{
					printf("FOUND le object :3\n");
					// Parse nested object
					pbr->baseColorTexture = atoi(json + tokens[i+3].start);
					int baseTextureEnd = i + calculateTotalTokenSize(tokens, i + 1);
					i = baseTextureEnd; // Skip the nested object
				} else
				{
					printf("NOT found le object >:3\n");
					i += 2; // Simple key-value pair
				}
				break;

			// !TODO The actual PBR values are FUCKED :3
			case GLTF_METALLICFACTOR:
				pbr->metallicFactor = atof(json + valueToken.start);
				i += 2;
				break;

			case GLTF_ROUGHNESSFACTOR:
				pbr->roughnessFactor = atof(json + valueToken.start);
				i += 2;
				break;

			default:
				keyIncrement = getIncrement(json, &tokens[i]);
				if (keyIncrement == 0)
				{
					printf("FATAL ERROR: key-value token mismatch, pbr!\n");
					exit(-100);
				}
				i += keyIncrement; // Handle unknown properties
				break;
		}
	}
}

void helperParseMaterials(const char *json, jsmntok_t *tokens, GltfMaterial *materials)
{
	int currentTokenIndex = 1; // Start from the first token after the materials array token
	int nameLength = 0;
	int pairIndex, keyTokenIndex, keyLength;
	uint32_t hash, keyIncrement;
	jsmntok_t keyToken, valueToken;
	jsmntok_t *materialToken;

	for (int materialIndex = 0; materialIndex < tokens[0].size; materialIndex++)
	{
		materialToken = &tokens[currentTokenIndex];
		uint32_t materialSize = calculateTotalTokenSize(tokens, currentTokenIndex);

		if (materialToken->type != JSMN_OBJECT)
		{
			continue; // Skip non-object tokens
		}

		materials[materialIndex].doubleSided = false;
		materials[materialIndex].name = NULL;
		materials[materialIndex].pbr = (PbrMetallicRoughness){0, 0.0f, 0.0f};

		pairIndex = 0;
		keyTokenIndex = currentTokenIndex + 1;

		while (pairIndex < materialToken->size)
		{
			keyToken = tokens[keyTokenIndex];
			valueToken = tokens[keyTokenIndex + 1];

			keyLength = keyToken.end - keyToken.start;
			char key[keyLength + 1];
			memcpy(key, json + keyToken.start, keyLength);
			key[keyLength] = '\0';
			hash = keyHash(key);

			switch(hash)
			{
				case GLTF_DOUBLESIDED:
					materials[materialIndex].doubleSided = (strncmp(json + valueToken.start, "true", 4) == 0);
					break;

				case GLTF_NAME:
					nameLength = valueToken.end - valueToken.start;
					materials[materialIndex].name = malloc(nameLength + 1);
					memcpy(materials[materialIndex].name, json + valueToken.start, nameLength);
					materials[materialIndex].name[nameLength] = '\0';
					break;

				case GLTF_PBRMETALICROUGHNESS:
					if (valueToken.type == JSMN_OBJECT)
					{
						parsePbrMetallicRoughness(json, &tokens[keyTokenIndex + 1], &materials[materialIndex].pbr);
					}
					break;

				default:
					break;
			}

			// Update indices for next iteration
			pairIndex++;
			keyIncrement = getIncrement(json, &tokens[keyTokenIndex]);
			keyTokenIndex += keyIncrement;
			if (keyIncrement == 0)
			{
				printf("FATAL ERROR: key-value token mismatch, material#%d!\n", materialIndex);
				exit(-100);
			}
		}

		// Move to the next material object token
		currentTokenIndex += materialSize;
	}
}

void parsePrimitive(const char *json, jsmntok_t *tokens, GltfPrimitive *primitive)
{
	int end = calculateTotalTokenSize(&tokens[0], 0); // Calculate the end index for the Primitive object
	//printf("THE END IS: %d\n", end);
	for (int i = 1; i < end;) { // Process each key-value pair within the Primitive object
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
			int attributesEnd = calculateTotalTokenSize(&tokens[i+1], 0) + 2; // End index for the attributes object
			printf("AttributesEnd: %d\n", attributesEnd);
			for (int j = i + 2; j < attributesEnd; j += getIncrement(json, &tokens[j]))
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
			i = attributesEnd;
			//end = end + 6;
			//i = attributesEnd - 2; // Correctly position i to the token after the attributes object
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
			uint32_t keyIncrement = getIncrement(json, &tokens[i]);
			i += keyIncrement;
			if (keyIncrement == 0)
			{
				printf("FATAL ERROR: key-value token mismatch, mesh primitive#%d!\n", i);
				exit(-100);
			}
		}
	}
}

void helperParseMeshes(const char *json, jsmntok_t *tokens, GltfMesh *meshes)
{
	int currentTokenIndex = 1;
	int indexIncrement = 0;

	uint32_t barrier = calculateTotalTokenSize(&tokens[0], 0);
	//printf("Barrier: %d\n", barrier);

	for (int meshIndex = 0; meshIndex < tokens[0].size; meshIndex++)
	{
		jsmntok_t *meshToken = &tokens[currentTokenIndex];

		if (meshToken->type != JSMN_OBJECT)
		{
			printf("Unexpected token type for mesh.\n");
			continue;
		}

		meshes[meshIndex].name = NULL;
		meshes[meshIndex].primitives = NULL;
		meshes[meshIndex].primitiveCount = 0;

		int meshTokenEnd = currentTokenIndex + calculateTotalTokenSize(tokens, currentTokenIndex);

		for (int i = currentTokenIndex + 1; i < meshTokenEnd;)
		{
			jsmntok_t keyToken = tokens[i];
			jsmntok_t valueToken = tokens[i + 1];

			int keyLength = keyToken.end - keyToken.start;
			char key[keyLength + 1];
			memcpy(key, json + keyToken.start, keyLength);
			key[keyLength] = '\0';
			uint32_t hash = keyHash(key);

			switch(hash)
			{
				case GLTF_NAME:
					// Name parsing logic...
					break;

				case GLTF_PRIMITIVES:
					//printf("HIT PRIMITIVES\n");
					if (valueToken.type == JSMN_ARRAY)
					{
						meshes[meshIndex].primitiveCount = valueToken.size;
						meshes[meshIndex].primitives = malloc(sizeof(GltfPrimitive) * valueToken.size);

						int primitiveTokenIndex = i + 2;
						for (int j = 0; j < valueToken.size; j++)
						{
							parsePrimitive(json, &tokens[primitiveTokenIndex], &meshes[meshIndex].primitives[j]);
							primitiveTokenIndex += calculateTotalTokenSize(tokens, primitiveTokenIndex);
						}
						indexIncrement = getIncrement(json, &tokens[i]) + 1;
						//printf("INDEX INCREMENT IS: %d\n", indexIncrement);
						i += indexIncrement;
					}
					break;

				default:
					// Other cases...
					break;
			}

			if (i > barrier)
			{
				break;
			}

			uint32_t keyIncrement = getIncrement(json, &tokens[i]);
			i += keyIncrement;
			if (keyIncrement == 0)
			{
				printf("FATAL ERROR: key-value token mismatch, mesh#%d!\n", meshIndex);
				exit(-100);
			}
		}

		currentTokenIndex = meshTokenEnd;
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

		int textureTokenEnd = currentTokenIndex + calculateTotalTokenSize(tokens, currentTokenIndex);

		for (int i = currentTokenIndex + 1; i < textureTokenEnd;)
		{
			jsmntok_t keyToken = tokens[i];
			jsmntok_t valueToken = tokens[i + 1];

			int keyLength = keyToken.end - keyToken.start;
			char key[keyLength + 1];
			memcpy(key, json + keyToken.start, keyLength);
			key[keyLength] = '\0';

			uint32_t hash = keyHash(key);

			switch (hash)
			{
				case GLTF_SAMPLER:
					textures[textureIndex].sampler = (uint32_t)strtol(json + valueToken.start, NULL, 10);
					break;

				case GLTF_SOURCE:
					textures[textureIndex].source = (uint32_t)strtol(json + valueToken.start, NULL, 10);
					break;

				default:
					break;
			}

			uint32_t keyIncrement = getIncrement(json, &tokens[i]);
			i += keyIncrement;
			if (keyIncrement == 0)
			{
				printf("FATAL ERROR: key-value token mismatch, texture#%d!\n", textureIndex);
				exit(-100);
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

		int imageTokenEnd = currentTokenIndex + calculateTotalTokenSize(tokens, currentTokenIndex);

		for (int i = currentTokenIndex + 1; i < imageTokenEnd;)
		{
			jsmntok_t keyToken = tokens[i];
			jsmntok_t valueToken = tokens[i + 1];

			int keyLength = keyToken.end - keyToken.start;
			char key[keyLength + 1];
			memcpy(key, json + keyToken.start, keyLength);
			key[keyLength] = '\0';

			uint32_t hash = keyHash(key);

			switch (hash)
			{
				case GLTF_MIMETYPE:
					images[imageIndex].mimeType = extractJsonString(json, &valueToken);
					break;

				case GLTF_NAME:
					images[imageIndex].name = extractJsonString(json, &valueToken);
					break;

				case GLTF_URI:
					images[imageIndex].uri = extractJsonString(json, &valueToken);
					break;

				default:
					break;
			}

			uint32_t keyIncrement = getIncrement(json, &tokens[i]);
			i += keyIncrement;
			if (keyIncrement == 0)
			{
				printf("FATAL ERROR: key-value token mismatch, image#%d!\n", imageIndex);
				exit(-100);
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
		int actualSize = calculateTotalTokenSize(tokens, currentTokenIndex);
		int accessorTokenEnd = currentTokenIndex + actualSize;

		for (int i = currentTokenIndex + 1; i < accessorTokenEnd;)
		{
			jsmntok_t keyToken = tokens[i];
			jsmntok_t valueToken = tokens[i + 1];

			int keyLength = keyToken.end - keyToken.start;
			char key[keyLength + 1];
			memcpy(key, json + keyToken.start, keyLength);
			key[keyLength] = '\0';
			uint32_t hash = keyHash(key);

			switch (hash)
			{
				case GLTF_BUFFERVIEW:
					tempAccessor.bufferView = (uint32_t)strtol(json + valueToken.start, NULL, 10);
					break;

				case GLTF_COMPONENTTYPE:
					tempAccessor.componentType = (uint32_t)strtol(json + valueToken.start, NULL, 10);
					break;

				case GLTF_COUNT:
					tempAccessor.count = (uint32_t)strtol(json + valueToken.start, NULL, 10);
					break;

				case GLTF_MAX:
					if (valueToken.type == JSMN_ARRAY)
					{
						parseAccessorMaxMin(json, &tokens[i + 2], tempAccessor.max, valueToken.size);
						i += 2 + valueToken.size; // Position after the max array
					}
					break;

				case GLTF_MIN:
					if (valueToken.type == JSMN_ARRAY)
					{
						parseAccessorMaxMin(json, &tokens[i + 2], tempAccessor.min, valueToken.size);
						i += 2 + valueToken.size; // Position after the min array
					}
					break;

				case GLTF_TYPE:
					strncpy(tempAccessor.type, json + valueToken.start, valueToken.end - valueToken.start);
					tempAccessor.type[valueToken.end - valueToken.start] = '\0';
					break;

				default:
					break;
			}

			uint32_t keyIncrement = getIncrement(json, &tokens[i]);
			i += keyIncrement;
			if (keyIncrement == 0)
			{
				printf("FATAL ERROR: key-value token mismatch, accessor#%d!\n", accessorIndex);
				exit(-100);
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

		int bufferViewTokenEnd = currentTokenIndex + calculateTotalTokenSize(tokens, currentTokenIndex);

		for (int i = currentTokenIndex + 1; i < bufferViewTokenEnd;)
		{
			jsmntok_t keyToken = tokens[i];
			jsmntok_t valueToken = tokens[i + 1];

			int keyLength = keyToken.end - keyToken.start;
			char key[keyLength + 1];
			memcpy(key, json + keyToken.start, keyLength);
			key[keyLength] = '\0';

			uint32_t hash = keyHash(key);

			switch (hash)
			{
				case GLTF_BUFFER:
					tempBufferView.index = (uint32_t)strtol(json + valueToken.start, NULL, 10);
					break;

				case GLTF_BYTELENGTH:
					tempBufferView.byteLength = (uint64_t)strtoll(json + valueToken.start, NULL, 10);
					break;

				case GLTF_BYTEOFFSET:
					tempBufferView.byteOffset = (uint64_t)strtoll(json + valueToken.start, NULL, 10);
					break;

				default:
					break;
			}

			uint32_t keyIncrement = getIncrement(json, &tokens[i]);
			i += keyIncrement;
			if (keyIncrement == 0)
			{
				printf("FATAL ERROR: key-value token mismatch, bufferView#%d!\n", bufferViewIndex);
				exit(-100);
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

		int samplerTokenEnd = currentTokenIndex + calculateTotalTokenSize(tokens, currentTokenIndex);

		for (int i = currentTokenIndex + 1; i < samplerTokenEnd;)
		{
			jsmntok_t keyToken = tokens[i];
			jsmntok_t valueToken = tokens[i + 1];

			int keyLength = keyToken.end - keyToken.start;
			char key[keyLength + 1];
			memcpy(key, json + keyToken.start, keyLength);
			key[keyLength] = '\0';

			uint32_t hash = keyHash(key);

			switch (hash)
			{
				case GLTF_MAGFILTER:
					tempSampler.magFilter = (uint32_t)strtol(json + valueToken.start, NULL, 10);
					break;

				case GLTF_MINFILTER:
					tempSampler.minFilter = (uint32_t)strtol(json + valueToken.start, NULL, 10);
					break;

				default:
					break;
			}

			uint32_t keyIncrement = getIncrement(json, &tokens[i]);
			i += keyIncrement;
			if (keyIncrement == 0)
			{
				printf("FATAL ERROR: key-value token mismatch, sampler#%d!\n", samplerIndex);
				exit(-100);
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

		int bufferTokenEnd = currentTokenIndex + calculateTotalTokenSize(tokens, currentTokenIndex);

		for (int i = currentTokenIndex + 1; i < bufferTokenEnd;)
		{
			jsmntok_t keyToken = tokens[i];
			jsmntok_t valueToken = tokens[i + 1];

			int keyLength = keyToken.end - keyToken.start;
			char key[keyLength + 1];
			memcpy(key, json + keyToken.start, keyLength);
			key[keyLength] = '\0';

			uint32_t hash = keyHash(key);

			switch (hash)
			{
				case GLTF_BYTELENGTH:
					tempBuffer.byteLength = (uint64_t)strtoll(json + valueToken.start, NULL, 10);
					break;

				case GLTF_URI:
					tempBuffer.uri = extractJsonString(json, &valueToken);
					break;

				default:
					break;
			}

			uint32_t keyIncrement = getIncrement(json, &tokens[i]);
			i += keyIncrement;
			if (keyIncrement == 0)
			{
				printf("FATAL ERROR: key-value token mismatch, buffer#%d!\n", bufferIndex);
				exit(-100);
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
	tokens = (jsmntok_t *)malloc(sizeof(jsmntok_t) * (tokenCount + 1));

	// Pass 2: Parse the JSON and fill the token array
	jsmn_parse(&parser, json, strlen(json), tokens, tokenCount);
	
	jsmntok_t endToken = {.type = JSMN_UNDEFINED, .start = 0, .end = 0, .size = 0};
	tokens[tokenCount] = endToken;
	printf("End token type: %d\n", tokens[tokenCount].type);

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

// Element processing functions

void* readFile(const char* filename, uint64_t* length)
{
	FILE* file = fopen(filename, "rb");
	if (!file)
	{
		perror("Error opening file");
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	*length = ftell(file);
	fseek(file, 0, SEEK_SET);

	void* data = malloc(*length);
	if (!data)
	{
		perror("Error allocating memory");
		fclose(file);
		return NULL;
	}

	fread(data, 1, *length, file);
	fclose(file);
	return data;
}

void processGltfBuffers(GltfElements* elements)
{
	for (uint32_t i = 0; i < elements->bufferCount; ++i)
	{
		GltfBuffer* buffer = &elements->buffers[i];
		if (buffer->uri != NULL)
		{
			uint64_t length = 0;
			void* data = readFile(buffer->uri, &length);

			if (data != NULL && length == buffer->byteLength)
			{
				buffer->address = data;
			} else
			{
				// Handle error: file read error or size mismatch
				// Free data if necessary
			}
		}
		// Handle case where buffer->uri is NULL if needed
	}
}

void freeGltfBuffers(GltfElements* elements)
{
	for (uint32_t i = 0; i < elements->bufferCount; ++i)
	{
		GltfBuffer* buffer = &elements->buffers[i];
		if (buffer->address)
        {
            free(buffer->address);
        }
		// Handle case where buffer->uri is NULL if needed
	}
}

// Function to get the data pointer from a buffer view
void* getBufferData(GltfElements* elements, uint32_t bufferViewIndex)
{
	GltfBufferView* bufferView = &elements->bufferViews[bufferViewIndex];
	GltfBuffer* buffer = &elements->buffers[bufferView->index];
	return (void*)((char*)buffer->address + bufferView->byteOffset);
}

// Function to get position data
Vector3* getPositionData(GltfElements* elements, GltfAccessor* accessor, uint32_t index)
{
	void* bufferData = getBufferData(elements, accessor->bufferView);

	// !TODO add checks to ensure positions are VEC3, handle other cases
	if (accessor->componentType == GLTF_COMPONENT_TYPE_FLOAT)
	{
		float* floatData = (float*)bufferData;
		// Assuming each position is a Vector3
		return (Vector3*)&floatData[index * 3]; // 3 floats per position
	}

	// Handle other component types as needed
	return NULL;
}

// Function to get texture coordinate data
Vector2* getTexcoordData(GltfElements* elements, GltfAccessor* accessor, uint32_t index)
{
	void* bufferData = getBufferData(elements, accessor->bufferView);

	// !TODO add checks to ensure positions are VEC3, handle other cases
	if (accessor->componentType == GLTF_COMPONENT_TYPE_FLOAT)
	{
		float* floatData = (float*)bufferData;
		// Assuming each texcoord is a Vector2
		return (Vector2*)&floatData[index * 2]; // 2 floats per texcoord
	}

	// Handle other component types as needed
	return NULL;
}

// Function to create a vertex buffer with combined position and texcoord
bool createCombinedVertexBuffer(VulkanComponents* components, GltfElements* elements, GltfMesh* mesh)
{
	Vector3 defaultColor = {0.5f, 0.5f, 0.5f};

	for (uint32_t primitiveIndex = 0; primitiveIndex < mesh->primitiveCount; ++primitiveIndex)
	{
		GltfPrimitive* primitive = &mesh->primitives[primitiveIndex];
		
		// Access the accessor for position and texcoord of the current primitive
		GltfAccessor* positionAccessor = &elements->accessors[primitive->position];
		GltfAccessor* texcoordAccessor = &elements->accessors[primitive->texcoord];

		// Calculate the number of vertices
		uint32_t vertexCount = positionAccessor->count;

		// Allocate memory for the combined vertex data
		Vertex* vertices = malloc(sizeof(Vertex) * vertexCount);
		if (!vertices)
		{
			printf("Failed to allocate memory for vertices!");
			return false; // Allocation failed
		}

		// Combine position and texcoord data into vertices
		for (uint32_t i = 0; i < vertexCount; i++)
		{
			Vector3* positionData = getPositionData(elements, positionAccessor, i);
			Vector2* texcoordData = getTexcoordData(elements, texcoordAccessor, i);

			vertices[i].position = *positionData;
			vertices[i].texCoord = *texcoordData;
			vertices[i].color = defaultColor;
		}

		// Create and transfer data to vertex buffer for each primitive
		if (!createVertexBuffer(components, vertexCount, &primitive->vertex, &primitive->vertexMemory) ||
			!stagingTransfer(components, vertices, primitive->vertex, sizeof(Vertex) * vertexCount))
			{
			printf("Failed to create and transfer vertex buffer for primitive %d!\n", primitiveIndex);
			free(vertices);
			return false;
		}

		free(vertices);
	}

	return true;
}

uint16_t* getIndexData(GltfElements* elements, GltfAccessor* accessor, uint32_t index)
{
	void* bufferData = getBufferData(elements, accessor->bufferView);

	// Handle different component types
	switch (accessor->componentType)
	{
		case GLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
		{
			uint8_t* byteData = (uint8_t*)bufferData;
			// Convert to uint16_t if needed
			return (uint16_t*)&byteData[index];
		}
		case GLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
		{
			uint16_t* shortData = (uint16_t*)bufferData;
			return &shortData[index];
		}
		case GLTF_COMPONENT_TYPE_UNSIGNED_INT:
		{
			// Assuming you want to convert unsigned int to uint16_t
			uint32_t* intData = (uint32_t*)bufferData;
			// Make sure to handle potential data loss if converting from uint32_t to uint16_t
			static uint16_t convertedIndex;
			convertedIndex = (uint16_t)intData[index];
			return &convertedIndex;
		}
		// Add cases for other component types if needed
	}

	return NULL;
}

bool createIndexBufferForMesh(VulkanComponents* components, GltfElements* elements, GltfMesh* mesh)
{
	for (uint32_t primitiveIndex = 0; primitiveIndex < mesh->primitiveCount; ++primitiveIndex)
	{
		GltfPrimitive* primitive = &mesh->primitives[primitiveIndex];

		// Access the accessor for indices of the current primitive
		GltfAccessor* indexAccessor = &elements->accessors[primitive->indices];

		uint16_t* indices = malloc(sizeof(uint16_t) * indexAccessor->count);
		if (!indices)
		{
			return false; // Allocation failed
		}

		// Populate the index data from the accessor
		for (uint32_t i = 0; i < indexAccessor->count; i++)
		{
			uint16_t* indexData = getIndexData(elements, indexAccessor, i);
			if (indexData == NULL)
			{
				free(indices);
				return false;
			}
			indices[i] = *indexData;
		}

		primitive->indexCount = indexAccessor->count;

		// Create and transfer data to index buffer for each primitive
		if (!createIndexBuffer(components, indexAccessor->count, &primitive->index, &primitive->indexMemory) ||
			!stagingTransfer(components, indices, primitive->index, sizeof(uint16_t) * indexAccessor->count))
			{
			printf("Failed to create and transfer index buffer for primitive %d!\n", primitiveIndex);
			free(indices);
			return false;
		}

		free(indices);
	}

	return true;
}


bool uploadTextureDataToGPU(VulkanComponents* components, GltfElements* elements, GltfMesh* mesh)
{
	bool success = true;

	for (uint32_t primitiveIndex = 0; primitiveIndex < mesh->primitiveCount; ++primitiveIndex)
	{
		GltfPrimitive* primitive = &mesh->primitives[primitiveIndex];
		uint32_t materialIndex = primitive->material;

		if (materialIndex >= elements->materialCount)
		{
			continue; // Invalid material index
		}

		GltfMaterial* material = &elements->materials[materialIndex];
		uint32_t textureIndex = material->pbr.baseColorTexture;

		if (textureIndex >= elements->textureCount)
		{
			continue; // Invalid texture index
		}

		GltfTexture* texture = &elements->textures[textureIndex];
		uint32_t imageIndex = texture->source;

		if (imageIndex >= elements->imageCount)
		{
			continue; // Invalid image index
		}

		GltfImage* image = &elements->images[imageIndex];

		// Assuming flag16 is determined based on some criteria
		bool flag16 = false; // Set this flag as per your requirement

		// Create texture image for each primitive
		if(!texture->processed)
		{
			if (!createTextureImage(components, &texture->textureImage, &texture->textureImageMemory, &texture->textureImageView, image->uri, flag16))
			{
				success = false;
			}

			// Create texture image view for each primitive
			/*if (!createTextureImageView(components, texture->textureImage, &texture->textureImageView))
			{
				success = false;
			}*/
			texture->processed = true;
			printf("Uploaded texture #%d!\n", primitiveIndex);
		}

	}

	return success;
}

void processGltfMeshes (VulkanComponents* components, GltfElements* elements)
{
	// Iterate through all meshes
	for (uint32_t i = 0; i < elements->meshCount; i++)
	{
		// Create vertex buffer
		printf("Creating mesh#%d vertex buffer!\n", i);
		createCombinedVertexBuffer(components, elements, &elements->meshes[i]);

		// Create index buffer
		printf("Creating mesh#%d index buffer!\n", i);
		createIndexBufferForMesh(components, elements, &elements->meshes[i]);

		// Create texture buffer
		printf("Creating mesh#%d texture!\n", i);
		uploadTextureDataToGPU(components, elements, &elements->meshes[i]);
	} 
}

void packageRenderables(VulkanComponents* components, GltfElements* elements)
{
	// Count total primitives across all meshes
	uint32_t totalPrimitiveCount = 0;
	for (uint32_t meshIndex = 0; meshIndex < elements->meshCount; meshIndex++)
	{
		totalPrimitiveCount += elements->meshes[meshIndex].primitiveCount;
	}

	// Allocate EntityBuffer for all primitives
	components->renderComp.buffers.entityCount = totalPrimitiveCount;
	printf("Allocating memory for %d entities!\n", totalPrimitiveCount);
	components->renderComp.buffers.entities = (EntityBuffer*)calloc(totalPrimitiveCount, sizeof(EntityBuffer));

	// Iterate through all meshes and their primitives
	uint32_t entityIndex = 0;
	for (uint32_t meshIndex = 0; meshIndex < elements->meshCount; meshIndex++)
	{
		GltfMesh* mesh = &elements->meshes[meshIndex];

		for (uint32_t primitiveIndex = 0; primitiveIndex < mesh->primitiveCount; primitiveIndex++)
		{
			GltfPrimitive* primitive = &mesh->primitives[primitiveIndex];

			// Set vertex, index, and texture buffers for each primitive
			printf("Setting entity#%d buffers and textures!\n", entityIndex);
			components->renderComp.buffers.entities[entityIndex].vertex = primitive->vertex;
			components->renderComp.buffers.entities[entityIndex].vertexMemory = primitive->vertexMemory;
			components->renderComp.buffers.entities[entityIndex].indexCount = primitive->indexCount;
			components->renderComp.buffers.entities[entityIndex].index = primitive->index;
			components->renderComp.buffers.entities[entityIndex].indexMemory = primitive->indexMemory;

			GltfMaterial* material = &elements->materials[primitive->material];
			GltfTexture* texture = &elements->textures[material->pbr.baseColorTexture];

			components->renderComp.buffers.entities[entityIndex].textureImage = texture->textureImage;
			components->renderComp.buffers.entities[entityIndex].textureImageMemory = texture->textureImageMemory;
			components->renderComp.buffers.entities[entityIndex].textureImageView = texture->textureImageView;

			entityIndex++;
		}
	}

	// TODO: Add render support for PBR parameters
	// TODO: Figure out what to do with normals and rotation
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
		//printGltfPrimitive(&mesh.primitives);
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
		printf("  Byte Length: %lu\n", bufferView.byteLength);
		printf("  Byte Offset: %lu\n", bufferView.byteOffset);
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
		printf("  Byte Length: %lu\n", buffer.byteLength);
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

char* readGltfFile(const char* filename, size_t* size)
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


bool parseGltf(VulkanComponents* components, const char* fileName)
{
	GltfElements elements;
	
	// Open file
	size_t fileSize;
	char* fileBuffer = readGltfFile(fileName, &fileSize);
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

	processGltfBuffers(&elements);
	processGltfMeshes (components, &elements);
	packageRenderables(components, &elements);
	printf("Loading complete!\nIf this crashes now, it's due to GPU buffer linking/access errors!\n");
    free(fileBuffer);
    freeGltfBuffers(&elements);
    freeGltfElements(&elements);
	// Allocate element buffers
	// Populate element buffers with parameters

	// Create renderable entity packages
	return true;
}
