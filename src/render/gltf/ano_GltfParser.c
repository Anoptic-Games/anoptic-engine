/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "ano_GltfParser.h"

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
            // Check if the next token is an array
            if (tokens[i + 1].type == JSMN_ARRAY)
            {
                if (strncmp(json + tokens[i].start, "scenes", tokens[i].end - tokens[i].start) == 0)
                {
                    elements->sceneCount = tokens[i + 1].size;
                } else if (strncmp(json + tokens[i].start, "nodes", tokens[i].end - tokens[i].start) == 0)
                {
                    elements->nodeCount = tokens[i + 1].size;
                } else if (strncmp(json + tokens[i].start, "materials", tokens[i].end - tokens[i].start) == 0)
                {
                    elements->materialCount = tokens[i + 1].size;
                } else if (strncmp(json + tokens[i].start, "meshes", tokens[i].end - tokens[i].start) == 0)
                {
                    elements->meshCount = tokens[i + 1].size;
                } else if (strncmp(json + tokens[i].start, "textures", tokens[i].end - tokens[i].start) == 0)
                {
                    elements->textureCount = tokens[i + 1].size;
                } else if (strncmp(json + tokens[i].start, "images", tokens[i].end - tokens[i].start) == 0)
                {
                    elements->imageCount = tokens[i + 1].size;
                } else if (strncmp(json + tokens[i].start, "accessors", tokens[i].end - tokens[i].start) == 0)
                {
                    elements->accessorCount = tokens[i + 1].size;
                } else if (strncmp(json + tokens[i].start, "bufferViews", tokens[i].end - tokens[i].start) == 0)
                {
                    elements->bufferViewCount = tokens[i + 1].size;
                } else if (strncmp(json + tokens[i].start, "samplers", tokens[i].end - tokens[i].start) == 0)
                {
                    elements->samplerCount = tokens[i + 1].size;
                } else if (strncmp(json + tokens[i].start, "buffers", tokens[i].end - tokens[i].start) == 0)
                {
                    elements->bufferCount = tokens[i + 1].size;
                }

                // Repeat for any other elements
            }
        }
    }

    free(tokens);

    return true;
}

void initializeGltfElements(GltfElements *elements) {
    if (elements == NULL) {
        // Handle null pointer if necessary
        return;
    }

    // Allocate memory for scenes
    if (elements->sceneCount > 0) {
        elements->scenes = (GltfScene*)malloc(elements->sceneCount * sizeof(GltfScene));
    }

    // Allocate memory for nodes
    if (elements->nodeCount > 0) {
        elements->nodes = (GltfNode*)malloc(elements->nodeCount * sizeof(GltfNode));
    }

    // Allocate memory for materials
    if (elements->materialCount > 0) {
        elements->materials = (GltfMaterial*)malloc(elements->materialCount * sizeof(GltfMaterial));
    }

    // Allocate memory for meshes
    if (elements->meshCount > 0) {
        elements->meshes = (GltfMesh*)malloc(elements->meshCount * sizeof(GltfMesh));
    }

    // Allocate memory for textures
    if (elements->textureCount > 0) {
        elements->textures = (GltfTexture*)malloc(elements->textureCount * sizeof(GltfTexture));
    }

    // Allocate memory for images
    if (elements->imageCount > 0) {
        elements->images = (GltfImage*)malloc(elements->imageCount * sizeof(GltfImage));
    }

    // Allocate memory for accessors
    if (elements->accessorCount > 0) {
        elements->accessors = (GltfAccessor*)malloc(elements->accessorCount * sizeof(GltfAccessor));
    }

    // Allocate memory for bufferViews
    if (elements->bufferViewCount > 0) {
        elements->bufferViews = (GltfBufferView*)malloc(elements->bufferViewCount * sizeof(GltfBufferView));
    }

    // Allocate memory for samplers
    if (elements->samplerCount > 0) {
        elements->samplers = (GltfSampler*)malloc(elements->samplerCount * sizeof(GltfSampler));
    }

    // Allocate memory for buffers
    if (elements->bufferCount > 0) {
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
            strncpy(key, json + key_token.start, key_length);
            key[key_length] = '\0'; // Null terminate the string

            // Match the key and populate the scene struct based on the key
            if (strncmp(key, "name", key_length) == 0)
            {
                // Handle the "name" key
                int name_length = value_token.end - value_token.start;
                char name[name_length + 1]; // +1 for the null terminator
                strncpy(name, json + value_token.start, name_length);
                name[name_length] = '\0'; // Null terminate the string

                // Assign the name to the scene struct
                strncpy(scenes[sceneIndex].name, name, name_length + 1);
            }
            else if (strncmp(key, "nodeCount", key_length) == 0)
            {
                // Handle the "nodeCount" key
                scenes[sceneIndex].nodeCount = strtol(json + value_token.start, NULL, 10);
            }
            // Add more else if blocks for other keys like "nodes", etc.
        }

        sceneIndex++;
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
		    // Check the string to determine the type of the next object
		    if (strncmp(json + tokens[i].start, "scenes", tokens[i].end - tokens[i].start) == 0)
		    {
		        // The next token should be the object/array associated with "scenes"
		        if (i + 1 < tokenCount && tokens[i + 1].type == JSMN_ARRAY)
		        {
		            // Call the helper function with the array token
		            helperParseScenes(json, &tokens[i + 1], elements->scenes);
		            // Skip over the array token as it's already processed
		            i++;
		        }
		    }
		    // Add similar checks for other keys ("nodes", "materials", etc.)
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
}

char* readFile(const char* filename, size_t* size) {
    FILE* file;
    char* buffer;
    size_t fileSize;

    // Open the file
    file = fopen(filename, "rb"); // Open in binary mode for portability
    if (file == NULL) {
        printf("Error opening file: %s\n", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    rewind(file);

    buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        printf("Error allocating memory for file: %s\n", filename);
        fclose(file);
        return NULL;
    }

    if (fread(buffer, 1, fileSize, file) != fileSize) {
        printf("Error reading file: %s\n", filename);
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[fileSize] = '\0';

    fclose(file);

    if (size != NULL) {
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
}
