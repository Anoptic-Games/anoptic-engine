/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// macOS: the shared POSIX handler plus the two native pieces it needs. Module map from the dyld image list at init, one entry per executable segment. base is the slide: recorded offsets are unslid, fed straight to atos -o <module>.

#include "blackbox/blackbox_posix.h"

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <sys/ucontext.h>

static void bb_modmap_build(void)
{
    uint32_t nimg = _dyld_image_count();
    for (uint32_t i = 0; i < nimg; i++) {
        const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(i);
        const char *name = _dyld_get_image_name(i);
        if (mh == NULL || mh->magic != MH_MAGIC_64)
            continue;
        uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(i);
        const struct load_command *lc = (const struct load_command *)(mh + 1);
        for (uint32_t c = 0; c < mh->ncmds; c++) {
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
                if ((seg->initprot & VM_PROT_EXECUTE) && seg->vmsize > 0)
                    bb_mod_add(slide + (uintptr_t)seg->vmaddr,
                               slide + (uintptr_t)seg->vmaddr + (uintptr_t)seg->vmsize,
                               slide, name != NULL ? name : "?");
            }
            lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
        }
    }
}

// Inputs: the handler's ucontext. Outputs: the interrupted thread's pc and fp, plus lr on arm64 (0 elsewhere). The Darwin getter macros handle the opaque arm64e thread state.
static void bb_crash_regs(void *uctx, uintptr_t *pc, uintptr_t *fp, uintptr_t *lr)
{
    const ucontext_t *uc = uctx;
#if defined(__arm64__) || defined(__aarch64__)
    *pc = (uintptr_t)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
    *fp = (uintptr_t)__darwin_arm_thread_state64_get_fp(uc->uc_mcontext->__ss);
    *lr = (uintptr_t)__darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss);
#elif defined(__x86_64__)
    *pc = (uintptr_t)uc->uc_mcontext->__ss.__rip;
    *fp = (uintptr_t)uc->uc_mcontext->__ss.__rbp;
    *lr = 0;
#else
#error "blackbox: teach bb_crash_regs this architecture's thread state"
#endif
}
