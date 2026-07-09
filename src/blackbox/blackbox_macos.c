/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// macOS: Darwin ships the same sigaction/sigaltstack/execinfo surface, so the shared POSIX
// implementation compiles verbatim. _DARWIN_C_SOURCE reopens the full namespace that the engine-wide
// _POSIX_C_SOURCE strictness would otherwise close on Darwin headers.

#define _DARWIN_C_SOURCE
#include "blackbox/blackbox_posix.h"
