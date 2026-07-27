/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Per-binary sanitizer options for tests that spawn engine threads.
//
// ASan <=21.x unmaps whatever alternate signal stack is installed when a thread
// exits, even one it never allocated:
//
//     void UnsetAlternateSignalStack() {
//       ...
//       CHECK_EQ(0, sigaltstack(&altstack, &oldstack));
//       UnmapOrDie(oldstack.ss_sp, oldstack.ss_size);   // unconditional
//     }
//
// Every engine thread arms its own crash-handler alt stack (bb_thread_arm) out
// of mimalloc, so ASan munmaps a live page out of mimalloc's arena. The next
// thread to allocate there writes its free list into the hole and takes a SEGV
// inside mi_page_extend_free -- far from the actual fault.
//
// Fixed upstream by llvm/llvm-project#179000 ("only unmap stacks the runtime has
// actually mapped"), which is on main but in no release as of compiler-rt 21.1.8.
// Until that ships, opt affected test binaries out of ASan's alt stack. Scope is
// deliberately per-binary: use_sigaltstack=0 breaks ASan's longjmp handling in
// anotest_text (FreeType), so it must not become a global default.
//
// Include once per test executable (each test is a single translation unit).
// Compiles to nothing without ASan.

#ifndef ANOPTIC_TEST_TEMPLATES_SANOPTS_H
#define ANOPTIC_TEST_TEMPLATES_SANOPTS_H

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
const char *__asan_default_options(void);
const char *__asan_default_options(void)
{
    return "use_sigaltstack=0";
}
#  endif
#endif

#endif // ANOPTIC_TEST_TEMPLATES_SANOPTS_H
