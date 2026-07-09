/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTIC_BLACKBOX_H
#define ANOPTIC_BLACKBOX_H

// !! COMMENTS IN THIS FILE STAY! You may add new ones

// Crash Trace Functionality

// Scenario: Oh no! The engine writes out of a legal buffer, seizes up, and crashes! The logger is stuck in a spinlock and the queue never get's drain()'d, so no last message can be sent to tell the world what happened... unless?

// Enter: anoptic_blackbox.h


// Stage 1: Initializing the blackbox.
// Captures the SIGSEGV, SIGABRT, SIGFPE, etc.
// Loads up the blackbox infrastructure.


// Stage 2: Hopefully never, but...
// A crash occurs. Immediately write trace to CRASH.log


// Stage 3: Immediately following stage 2, a hail mary ano_log_drain() to hopefully recover anything that might've survived in the log buffers, *after* we have already written the CRASH.log.

// Stage 4: Final, the anocraft accident investigation.
/// TODO: Ask user if they want to send telemetry (future implement)
// First boot post-crash, investigate what happened, append it to logs in further details if necessary/possible/desireable.


/* Lifecycle Functions */

// Arm the blackbox: run the Stage 4 check for a previous run's CRASH.log, then install the Stage 1 hooks.
// Call once from main, right after ano_log_init. The hooks live for the whole process, no cleanup.
// Output: 0 on success, -1 if a hook failed to install (the engine flies on, crash-naked).
int ano_blackbox_init(void);

// Arm the CALLING thread's crash stack: an alternate signal stack (POSIX) or a guaranteed
// stack-overflow handler reservation (win64), so a blown stack on this thread still yields a record.
// ano_thread_create arms every engine thread automatically; only threads created behind the engine's
// back (external libraries, OS callbacks) need to call this themselves. The hooks are process-wide
// and order-independent: arming works before or after ano_blackbox_init. Idempotent per thread.
// Never call on a thread armed by someone else (main is armed by ano_blackbox_init itself).
// Output: 0 on success, -1 if the OS refused (the thread reports crash-naked, the engine flies on).
int ano_blackbox_thread_arm(void);

// Release what ano_blackbox_thread_arm reserved, just before the thread exits. Safe to call unarmed.
void ano_blackbox_thread_disarm(void);

#endif