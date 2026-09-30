#ifndef MUSASHI_CPU_STATUS_H
#define MUSASHI_CPU_STATUS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MusashiCpuStatus MusashiCpuStatus;
typedef enum MusashiCpuContextProvenance {
    MUSASHI_CPU_CONTEXT_SOURCE = 1,
    MUSASHI_CPU_CONTEXT_HOST_BIOS_SERVICE = 2
} MusashiCpuContextProvenance;
/* Borrowed for begin only; identity remains stable through finish. HOST has
 * no physical PC/NPC/instruction/BD claim: those fields must all be zero. */
typedef struct MusashiCpuContext {
    const void *identity;
    const uint32_t *gpr; /* Exactly 32 live registers, copied by begin. */
    uint32_t pc, npc, instruction, branch_pc;
    int instruction_valid;
    int delay_slot;
    MusashiCpuContextProvenance provenance;
} MusashiCpuContext;
typedef enum MusashiCpuExceptionKind {
    MUSASHI_CPU_EXCEPTION_IRQ = 1,
    MUSASHI_CPU_EXCEPTION_SYS1 = 2,
    MUSASHI_CPU_EXCEPTION_SYS2 = 3
} MusashiCpuExceptionKind;
typedef struct MusashiCpuExceptionToken {
    const MusashiCpuStatus *owner;
    const void *identity;
    uint64_t sequence;
} MusashiCpuExceptionToken;
typedef struct MusashiCpuStatusSnapshot {
    uint32_t sr, saved_sr;
    uint32_t gpr[32];
    uint32_t pc, npc, instruction, branch_pc;
    uint32_t cause, epc;
    uint64_t sequence;
    const void *identity;
    MusashiCpuContextProvenance provenance;
    MusashiCpuExceptionKind kind;
    int instruction_valid, delay_slot, cause_epc_valid;
    int active, kernel_entered, sys_applied, faulted;
} MusashiCpuStatusSnapshot;
/* Callback and userdata outlive owner. No callback may reenter this owner.
 * out must point to NULL. Output remains untouched on refusal. Initial SR is selected ROM 40000000. */
int musashi_cpu_status_open(MusashiCpuStatus **out,
    uint64_t (*current_thread)(void *), void *userdata);
/* Close after fault may discard an active exceptional frame; it is teardown,
 * not RFE. Healthy active frame and wrong thread refuse. */
int musashi_cpu_status_close(MusashiCpuStatus *);
int musashi_cpu_status_snapshot(MusashiCpuStatus *, MusashiCpuStatusSnapshot *);
int musashi_cpu_status_read(MusashiCpuStatus *, uint32_t *);
int musashi_cpu_status_irq_eligible(MusashiCpuStatus *, int *);
/* Native BIOS policy / fixture adapter, NOT an implicit SYS or hook. */
int musashi_cpu_status_set_enabled(MusashiCpuStatus *, int enabled);
/* Bounded MTC0 Status: same value or old value OR CU2, outside exception. */
int musashi_cpu_status_write_source(MusashiCpuStatus *,
    const MusashiCpuContext *, uint32_t value);
/* hardware_pending is the CP0 external IP2 bit: 0 or 00000400, not I_STAT.
 * IRQ requires pending IP2 and current IEc+IM2 eligibility. HOST IRQ is a
 * native semantic service boundary with Cause/EPC explicitly invalid.
 * SOURCE SYS requires fetched SYSCALL and live a0 selector1/2. */
int musashi_cpu_status_begin(MusashiCpuStatus *, MusashiCpuExceptionKind,
    const MusashiCpuContext *, uint32_t hardware_pending,
    MusashiCpuExceptionToken *);
/* Checked active-token snapshot for image consumer. Valid after a fault too. */
int musashi_cpu_status_inspect(MusashiCpuStatus *,
    const MusashiCpuExceptionToken *, MusashiCpuStatusSnapshot *);
/* Called by actual published-image/kernel consumer once entry is accepted. */
int musashi_cpu_status_accept_kernel_entry(MusashiCpuStatus *,
    const MusashiCpuExceptionToken *);
/* SYS1 writes prior eligibility to result; SYS2 preserves *result (may NULL).
 * Apply after kernel entry/hook acceptance, once. IRQ cannot apply SYS. */
int musashi_cpu_status_apply_sys(MusashiCpuStatus *,
    const MusashiCpuExceptionToken *, int32_t *result);
int musashi_cpu_status_finish(MusashiCpuStatus *,
    const MusashiCpuExceptionToken *);
/* Explicit service refusal: preserve accepted state and mark sticky fault. */
int musashi_cpu_status_fault(MusashiCpuStatus *,
    const MusashiCpuExceptionToken *);
#ifdef MUSASHI_CPU_STATUS_TESTING
/* Synthetic fixture only; no production symbol or captured-state API. */
int musashi_cpu_status_open_fixture(MusashiCpuStatus **,
    uint64_t (*)(void *), void *, uint32_t initial_sr);
#endif
#ifdef __cplusplus
}
#endif
#endif
