/**
 * @file anoptic_memalign.h
 * @brief Platform-agnostic memory management API for the Anoptic Engine.
 */

// include guard
#ifndef ANOPTIC_MEMALIGN_H
#define ANOPTIC_MEMALIGN_H

#include <stddef.h> // for size_t

/**
 * @brief Allocates a block of @p size bytes of memory aligned to a @p alignment boundary.
 * 
 * This function provides a platform-agnostic interface for aligned memory allocation.
 *
 * @param size     The size of the memory block to allocate, in bytes.
 * @param alignment The alignment of the memory block that is to be allocated.
 *                  This must be an integer power of 2.
 *
 * @return A pointer to the first byte of the allocated memory block, or NULL if the
 *         allocation fails.
 * 
 * @note The function will return NULL if @p size or @p alignment is 0.
 */
void* anoptic_aligned_malloc(size_t size, size_t alignment);

/**
 * @brief Frees an aligned block of memory.
 * 
 * This function provides a platform-agnostic interface for freeing aligned memory blocks.
 * 
 * @param ptr Pointer to the memory block that needs to be freed.
 * 
 * @note It's undefined behavior to free a memory block that wasn't previously
 *       allocated with anoptic_aligned_malloc or equivalent.
 */
void anoptic_aligned_free(void* ptr);



#endif // ANOPTIC_MEMORY_H
// end of include guard, end of file