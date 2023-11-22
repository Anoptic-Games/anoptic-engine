/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifdef __linux__

#include "anoptic_memalign.h"
#include <stdlib.h>
#include <stdio.h>


// Linux-specific implementation of aligned_malloc as defined in the ano_memory API.
void* ano_aligned_malloc(size_t size, size_t alignment) {
    void* ptr = NULL;
	
	if (alignment < sizeof(void *)) // Minimum alignment size is void* 
	{
   		alignment = sizeof(void *);
	}

	size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);

	int result = posix_memalign(&ptr, alignment, aligned_size);
    if (result != 0) {
		printf("Size of void*: %zu\n", sizeof(void *));
		printf("Posix memalign error: %d\n", result);
        return NULL;  // posix_memalign will set errno appropriately
    }
    return ptr;
}

// Linux-specific implementation of aligned_free as defined in the ano_memory API.
void ano_aligned_free(void* ptr) {
    free(ptr);  // free works for memory allocated by posix_memalign
}



#endif
