/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/// \file
/// \brief Memory management API for aligned allocations.

#ifndef ANOPTIC_MEMALIGN_H
#define ANOPTIC_MEMALIGN_H

#include <stddef.h> // for size_t

/// Allocates aligned memory block.
/// \param size Bytes to allocate.
/// \param alignment Required alignment, must be power of 2.
/// \return Allocated memory or NULL.
/// \note NULL if size or alignment is 0.
void* ano_aligned_malloc(size_t size, size_t alignment);

/// Frees aligned memory block.
/// \param ptr Memory to free.
/// \note Undefined if ptr not from ano_aligned_malloc.
void ano_aligned_free(void* ptr);

#endif // ANOPTIC_MEMALIGN_H