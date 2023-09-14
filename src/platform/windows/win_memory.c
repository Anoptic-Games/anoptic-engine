#ifdef _WIN32

#include "anoptic_memalign.h"
#include <malloc.h>

// TODO: Figure out what this is lol

// Windows-specific implementation of aligned_malloc as defined in the anoptic_memory API.
void* anoptic_aligned_malloc(size_t size, size_t alignment) {
    return _aligned_malloc(size, alignment);
}

// Windows-specific implementation of aligned_free as defined in the anoptic_memory API.
void anoptic_aligned_free(void* ptr) {
    _aligned_free(ptr);
}



#endif