#ifndef MUSASHI_NATIVE_LANE_H
#define MUSASHI_NATIVE_LANE_H

/* Generic native lane: guest-PC keyed host implementations of guest
 * functions, consulted by formatter_step before it fetches a word, so the
 * startup CPU, the IRQ routers and nested source execution all use it.
 *
 * Register ABI (PS1 o32): a0-a3 are r[4..7], further arguments are words at
 * guest sp+0x10, +0x14, ... ; results go to v0/v1 (r[2], r[3]); on success
 * the lane returns to ra with the interpreter's delay-slot state clean.
 *
 * The implementation lives in mips_formatter.c, so every translation unit
 * that already links the formatter gets it without a new source file. */

#include <stddef.h>
#include <stdint.h>

#include "musashi_boot_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Guest main RAM must be host-mapped at this address for natively compiled
 * decomp C, which dereferences 32-bit guest addresses directly. */
#define MUSASHI_NATIVE_LANE_GUEST_BASE 0x80000000u

typedef struct MusashiNativeLaneCall {
    MusashiBootMemory *memory;
    uint32_t *r; /* the live guest register file, r[0..31] */
} MusashiNativeLaneCall;

/* A host implementation. Return 1 when the call completed (v0/v1 written as
 * needed), 0 to refuse; a refusal before any guest-visible side effect makes
 * the lane fall back to the interpreter. */
typedef int (*MusashiNativeLaneFn)(MusashiNativeLaneCall *call);

typedef struct MusashiNativeLaneEntry {
    uint32_t guest_pc;
    uint32_t cost_cycles; /* charged once to the execution clock per call */
    MusashiNativeLaneFn fn;
    const char *name;
    uint8_t argc;    /* o32 argument words the host function consumes */
    uint8_t returns; /* 1 when it produces v0, 0 for void */
} MusashiNativeLaneEntry;

/* Overlay functions (MAIN.CD / scene members) share load addresses, so an
 * overlay entry runs natively only when the bytes resident in guest RAM at
 * [guest_pc, guest_pc + size) hash to the member's own digest (FNV-1a 64,
 * computed at build time from the user's extracted member file). Checked on
 * every call, never inferred; on a mismatch the interpreter runs the words
 * that are actually there. Several entries may share a guest_pc (one per
 * member). */
typedef struct MusashiNativeLaneOverlayEntry {
    MusashiNativeLaneEntry entry;
    uint32_t size;   /* bytes hashed: the function's registry extent */
    uint64_t digest; /* FNV-1a 64 of the member's bytes over that extent */
    const char *member; /* e.g. "main_0003" */
} MusashiNativeLaneOverlayEntry;

/* Install the overlay table (copied and sorted; replaces any previous one).
 * Returns the number of entries, or -1 on an unaligned PC or a
 * (guest_pc, digest) duplicate. */
int musashi_native_lane_install_overlay(const MusashiNativeLaneOverlayEntry *entries,
                                        size_t count);

/* 1 when guest RAM at [pc, pc + size) hashes to digest (used by generated
 * stubs before a direct native call to another overlay function). */
int musashi_native_lane_overlay_resident(uint32_t pc, uint32_t size, uint64_t digest);

/* Overlay entries run natively (digest matched) and refused (a candidate PC
 * whose resident bytes matched no member), since process start. */
void musashi_native_lane_overlay_stats(uint64_t *verified, uint64_t *refused);

/* Argument n (0-based) under the o32 register ABI. */
uint32_t musashi_native_lane_arg(const MusashiNativeLaneCall *call, unsigned n);

/* Install one sorted-or-unsorted table (copied into an index; entries and
 * names must outlive the lane). Replaces any previous table. Returns the
 * number of accepted entries, or -1 on a duplicate/unaligned PC. */
int musashi_native_lane_install(const MusashiNativeLaneEntry *entries,
                                size_t count);

/* Nonzero when MUSASHI_NATIVE_LANE is not "0" and guest RAM is mapped at
 * MUSASHI_NATIVE_LANE_GUEST_BASE for this memory. */
int musashi_native_lane_usable(const MusashiBootMemory *memory);

/* Nesting suspension: while suspended, every PC runs on the interpreter.
 * Host routers whose checks count interpreted calls (the IRQ dispatcher)
 * suspend the lane for their duration. */
void musashi_native_lane_suspend(int suspend);

/* Interpreter thunk for natively compiled code: call guest function target
 * with argc words (a0-a3, then guest stack), on the invoking CPU and its
 * run loop, and return v0 (v1 through *v1 when non-NULL). A refusal inside
 * the guest callee aborts the whole native call (longjmp to the lane). Only
 * valid while a lane function is running. */
uint32_t musashi_native_guest_call(uint32_t target, unsigned argc,
                                   const uint32_t *args, uint32_t *v1);

/* Maps size bytes of shared memory at MUSASHI_NATIVE_LANE_GUEST_BASE (the
 * guest window: MusashiBootMemory first) and aliases its scratchpad page at
 * guest 0x1F800000. Returns the window or NULL; *scratchpad_aliased says
 * whether the alias took. */
void *musashi_native_lane_map_guest(size_t size, int *scratchpad_aliased);

/* Guest PC of the innermost running lane entry, or 0 (diagnostics). */
uint32_t musashi_native_lane_current(void);

/* Aborts the running native call (the lane step is refused). Only valid
 * while a lane function runs. */
void musashi_native_lane_abort(void);
/* Guarded 32-bit division for natively compiled code (tools/native_lane_gen.py
 * lowers variable divisors to these); a trapping divisor aborts the call. */
int32_t lane_sdiv32(int32_t a, int32_t b);
int32_t lane_srem32(int32_t a, int32_t b);
uint32_t lane_udiv32(uint32_t a, uint32_t b);
uint32_t lane_urem32(uint32_t a, uint32_t b);

/* Reference execution for differential checks: run guest function target
 * with argc argument words on a fresh device-less CPU (gp=0x80074750,
 * ra=sentinel, stack args at sp+0x10) until it returns. use_lane=0 forces
 * the interpreter for every PC. Returns 1 on a normal return. */
int musashi_native_lane_reference_call(MusashiBootMemory *memory, uint32_t target,
                                       const uint32_t *args, unsigned argc,
                                       uint32_t sp, int use_lane,
                                       uint32_t *v0, uint32_t *v1);

/* Executed-path tracer (MUSASHI_TRACE_FUNCS=path). Called by the formatter
 * for jal/jalr targets and CPU dispatch entries; exposed for host routers
 * that enter guest code without a jal. kind: 'j' jal, 'r' jalr, 'd' entry. */
void musashi_trace_function_entry(uint32_t target, uint32_t caller, char kind);
/* Write <path>.counts now (it is also written at exit). */
void musashi_trace_function_flush(void);

/* Guest function-entry hook for the platform layer (bfm_plat hook sites,
 * mods): called once when execution reaches a jal/jalr target, after the
 * delay slot (a0-a3 final) and before the native lane. regs is the live
 * guest register file, valid only during the call. NULL disables it. */
typedef void (*MusashiEntryHook)(void *userdata, uint32_t pc, const uint32_t *regs);
void musashi_formatter_set_entry_hook(MusashiEntryHook hook, void *userdata);

/* Overlay selection notice for the platform layer: name is the selected
 * member ("SC02_031", "SC01_000", "SC01_001", "MAIN_0004", "MAIN_0007",
 * "MAIN_0010") or NULL when none is selected. Called on changes only. */
typedef void (*MusashiOverlayHook)(void *userdata, const char *name);
void musashi_formatter_set_overlay_hook(MusashiOverlayHook hook, void *userdata);

#ifdef __cplusplus
}
#endif

#endif
