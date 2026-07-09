/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Linux: the shared POSIX implementation verbatim. backtrace_symbols_fd resolves exported symbols
// only; link the executable with -rdynamic if names beat module+offset in your CRASH.log.
// _DEFAULT_SOURCE reopens the XSI/misc namespace (sigaltstack, SA_ONSTACK, SS_DISABLE) that the
// engine-wide _POSIX_C_SOURCE strictness would otherwise close on glibc headers.

#define _DEFAULT_SOURCE
#include "blackbox/blackbox_posix.h"
