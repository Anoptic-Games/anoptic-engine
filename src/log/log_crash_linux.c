/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Linux: shared POSIX handler + dl_iterate_phdr module map. _GNU_SOURCE for REG_* / dl_iterate_phdr.

#define _GNU_SOURCE
#include "log/log_crash_posix.h"

#include <link.h>
#include <ucontext.h>

// One [lo, hi) per executable PT_LOAD. dlpi_addr = load bias. Main exe name "": data = /proc/self/exe.
static int bb_phdr_cb(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    const char *name = (info->dlpi_name != NULL && info->dlpi_name[0] != 0)
                     ? info->dlpi_name : (const char *)data;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_X))
            continue;
        uintptr_t lo = (uintptr_t)info->dlpi_addr + ph->p_vaddr;
        bb_mod_add(lo, lo + ph->p_memsz, (uintptr_t)info->dlpi_addr, name);
    }
    return 0;
}

static void bb_modmap_build(void)
{
    static char exe[256];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) exe[n] = 0;
    else       memcpy(exe, "exe", 4);
    dl_iterate_phdr(bb_phdr_cb, exe);
}

// Inputs: ucontext. Outputs: pc, fp, lr on aarch64 (0 elsewhere).
static void bb_crash_regs(void *uctx, uintptr_t *pc, uintptr_t *fp, uintptr_t *lr)
{
    const ucontext_t *uc = uctx;
#if defined(__x86_64__)
    *pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    *fp = (uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
    *lr = 0;
#elif defined(__aarch64__)
    *pc = (uintptr_t)uc->uc_mcontext.pc;
    *fp = (uintptr_t)uc->uc_mcontext.regs[29];
    *lr = (uintptr_t)uc->uc_mcontext.regs[30];
#else
#error "blackbox: teach bb_crash_regs this architecture's mcontext"
#endif
}
