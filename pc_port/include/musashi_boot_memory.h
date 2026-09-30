#ifndef MUSASHI_BOOT_MEMORY_H
#define MUSASHI_BOOT_MEMORY_H

#include "musashi_cpu_status.h"
#include <stddef.h>
#include <stdint.h>
#include "musashi_gpu_device.h"

#define MUSASHI_RAM_SIZE 0x200000u
#define MUSASHI_SCRATCHPAD_SIZE 0x400u

/* Verify the pinned full EXE, then map text/data only. Preserve all other RAM;
 * CRT clears, heap/stack setup and constructor effects belong to execution.
 * Rejection leaves RAM unchanged. This accepts no arbitrary retail image. */
struct MusashiBootMemory;
int musashi_boot_map_exe(struct MusashiBootMemory *, const uint8_t *, size_t);

/* Caller-owned, initialized backing storage. Byte views may overlap; never
 * cast them to host structs/pointers. Scalar access is explicitly little endian.
 * No synchronization: the owner must serialize all access and loading. */
typedef struct MusashiBootMemory {
    uint8_t bytes[MUSASHI_RAM_SIZE];
    uint8_t scratchpad[MUSASHI_SCRATCHPAD_SIZE];
} MusashiBootMemory;

typedef struct MusashiEntryState {
    uint32_t stack_address;
    uint32_t heap_address;
    uint32_t heap_size;
} MusashiEntryState;

/* Only the observed cached RAM window [80000000,80200000) is supported.
 * Zero-length, overflowing, MMIO, scratchpad, BIOS, and other segment spans
 * return NULL. This is not a general PS1 memory bus or function resolver. */
uint8_t *musashi_boot_ram_span(MusashiBootMemory *memory,
                              uint32_t address, size_t size);
/* Aligned 32-bit accesses. Return 1 on success, 0 without changing outputs or
 * memory on failure. Guest address words remain uint32_t, never host pointers. */
/* Halfword accesses share the byte backing with word accesses. Require an
 * even cached-RAM address; failure leaves memory and read output unchanged.
 * Device registers and unsupported guest aliases are not RAM accesses. */
int musashi_boot_read16(MusashiBootMemory *memory, uint32_t address,
                        uint16_t *value);
int musashi_boot_write16(MusashiBootMemory *memory, uint32_t address,
                         uint16_t value);
int musashi_boot_read32(MusashiBootMemory *memory, uint32_t address,
                        uint32_t *value);
int musashi_boot_write32(MusashiBootMemory *memory, uint32_t address,
                         uint32_t value);

/* Execute a guest callback against the same RAM. Return one only after the
 * actual bound callback returns; unknown targets or incomplete execution must
 * return zero. The target is never a host pointer. Callers supply an execution
 * router, not a replacement implementation of the handler below. */
typedef int (*MusashiGuestCallbackExecutor)(void *userdata,
    MusashiBootMemory *memory, uint32_t target);

/* Shared 80042CE8 VBlank handler: increment the guest tick with word wrapping,
 * then read each of its eight slots immediately before invoking it. A callback
 * may change later slots or the tick through the same RAM. Return zero on an
 * unavailable/refused execution, retaining the increment and earlier effects;
 * this failure is not a resumable frame and must not be blindly retried.
 * Empty slots need no executor. Owner serializes memory and callback access.
 * Calling this body does not generate a VBlank event or deliver an IRQ. */
int musashi_boot_call_80042ce8(MusashiBootMemory *memory,
    MusashiGuestCallbackExecutor execute, void *userdata);

/* Shared 8001096C VBlank slot: one jal to 8002CDD8. Return zero if that
 * target is refused; the tick increment in 80042CE8 is already visible. */
int musashi_boot_call_8001096c(MusashiBootMemory *memory,
    MusashiGuestCallbackExecutor execute, void *userdata);

/* Verify the complete registered SLUS_007.26 image before any write; then load
 * its text/data and apply the platform entry's observed memory effects. This
 * does not call game startup. Return 1 on success, 0 with memory/state unchanged
 * on failure. No arbitrary EXE layouts or caller-supplied hash overrides.
 *
 * memory must already contain initialized bytes; bytes outside the loaded and
 * entry-written regions are preserved, not assumed to match a BIOS snapshot.
 * incoming_ra must be supplied explicitly (synthetic tests are not authority).
 * state must be distinct from memory and exe. exe must remain stable throughout
 * this call; it may overlap memory because loading uses memmove. */
int musashi_boot_load_entry(MusashiBootMemory *memory, const uint8_t *exe,
                            size_t exe_size, uint32_t incoming_ra,
                            MusashiEntryState *state);

/* Invoke the decomp-owned 80043060 body against the word at 8006CBE8.
 * This isolated, call-free routine permits scalar copy-in/copy-out; it is not
 * a general binding scheme for aliases, callbacks, volatile data, or MMIO.
 * Return 1 on success; return 0 for null arguments without changing memory or
 * previous. previous must not overlap memory. Like the RAM API, calls must be
 * serialized. Does not execute the preceding CRT guard or advance startup. */
int musashi_boot_call_80043060(MusashiBootMemory *memory, int32_t argument,
                              int32_t *previous);

/* Execute the matched byte-fill helper directly on a bounded RAM view.
 * Uses the low byte of value, permits unaligned byte addresses. count==0 is a
 * no-op only for an address inside the supported RAM window. Returns 1 on
 * success, 0 without writes for null memory or an unsupported/overflowing span.
 * The guest address is never cast to a host pointer. */
int musashi_boot_call_8005c29c(MusashiBootMemory *memory, uint32_t address,
                              int32_t value, uint32_t count);

/* Execute the decomp-owned environment clear on a checked cached-RAM span.
 * The guest address is never cast to a host pointer. */
int musashi_boot_call_80016714(MusashiBootMemory *memory, uint32_t address,
                               uint32_t count);

/* Execute the decomp-owned 8001903C record clear at its observed guest
 * address. This is a bounded composition of the checked 80016714 binding. */
int musashi_boot_call_8001903c(MusashiBootMemory *memory);

/* Clear the two exact table targets used by 800142C8 through checked spans. */
int musashi_boot_call_80029044(MusashiBootMemory *memory);
int musashi_boot_call_8002906c(MusashiBootMemory *memory);

/* Exact 800426D4 ready-status leaf: zero-extended D_8006BAFE. */
int musashi_boot_call_800426d4(MusashiBootMemory *memory, uint16_t *result);

/* Execute the decomp-owned device query through explicit GPU callbacks.
 * This does not map MMIO into cached RAM or assume the emulator's observed
 * status value. */
/* Uses live GPU pointer cells in memory, with retail snapshot lifetimes;
 * callers must load/initialize those cells, not rely on fixed MMIO defaults. */
int musashi_boot_call_8005c1c0(MusashiBootMemory *memory,
                               const MusashiGpuDevice *device,
                              int32_t mode, int32_t *result);

/* Execute the decomp-owned low-level device reset against checked shared RAM
 * and explicit 16/32-bit device callbacks. The wrapper fails on an incomplete
 * adapter and never treats device addresses as cached RAM. */
int musashi_boot_call_8005bd7c(MusashiBootMemory *memory,
                              const MusashiGpuDevice *device,
                              int32_t mode, int32_t *result);

/* Execute the decomp-owned word-count clear through a checked cached-RAM
 * view. The count is in four-byte words, matching retail 80042C64. Nonzero
 * clears require word alignment; zero count accepts any cached-RAM byte
 * address without access. Invalid requests fail without modifying RAM. */
int musashi_boot_call_80042c64(MusashiBootMemory *memory, uint32_t address,
                              uint32_t word_count);

/* Execute the leaf graphics dispatch 8005B684 with its live control pointer.
 * The command's top byte selects one checked status byte; the low byte is
 * stored only after the explicit device write is accepted. */
int musashi_boot_call_8005b684(MusashiBootMemory *memory,
                               const MusashiGpuDevice *device,
                               uint32_t command);

typedef void (*MusashiConsoleSink)(void *userdata, const uint8_t *bytes,
                                   int32_t length);

/* Run the matched bounded byte search and translate its result to a guest
 * address (zero means no match). Null address or nonpositive count needs no
 * memory access. Otherwise the whole requested span must be cached RAM.
 * Return 0 on invalid inputs without changing result; 1 on completed search.
 * result must not overlap memory. This does not advance startup. */
int musashi_boot_search_bytes(MusashiBootMemory *memory, uint32_t address,
                              int32_t target, int32_t count, uint32_t *result);

/* Execute the decomp-owned character-output body against shared guest RAM.
 * The sink receives emitted bytes; classification and column state remain in
 * the RAM image. This binds only ordinary cached-RAM state and does not claim
 * that the BIOS console or formatter is implemented. */
int musashi_boot_console_char(MusashiBootMemory *memory, int32_t value,
                              MusashiConsoleSink sink, void *userdata);

/* Execute the matched console-string body for a guest cached-RAM address.
 * Both the source string and retail fallback address must be NUL-terminated
 * inside cached RAM; no host pointer is accepted from the caller. */
int musashi_boot_console_string(MusashiBootMemory *memory, uint32_t address,
                                MusashiConsoleSink sink, void *userdata);

/* Execute the exact 8005C640 MIPS instruction stream against guest format and
 * argument-word addresses. This is deliberately not a host-varargs API: its
 * arguments, strings, stores, and console state all remain in checked guest
 * RAM. The caller must have loaded the entry data containing the formatter's
 * conversion table. Returns 0 for an invalid guest span, unsupported control
 * flow, or an instruction budget overrun; result and RAM remain observable as
 * produced up to an execution fault. */
int musashi_boot_format_8005c640(MusashiBootMemory *memory,
                                 uint32_t format_address,
                                 uint32_t argument_words_address,
                                 MusashiConsoleSink sink, void *userdata,
                                 int32_t *result);

/* Execute the exact 8005C604 PS1 varargs home-area wrapper followed by the
 * exact 8005C640 stream. arg1..arg3 are raw 32-bit guest argument words, not
 * host pointers or varargs. This is the binding required by retail callers
 * such as ResetGraph before they can enter the formatter without an ABI shim. */
int musashi_boot_format_8005c604(MusashiBootMemory *memory,
                                 uint32_t format_address, uint32_t arg1,
                                 uint32_t arg2, uint32_t arg3,
                                 MusashiConsoleSink sink, void *userdata,
                                 int32_t *result);

typedef enum MusashiResetGraphPrefixStatus {
    MUSASHI_RESETGRAPH_PREFIX_INVALID_INPUT = 0,
    MUSASHI_RESETGRAPH_PREFIX_UNSUPPORTED_CALL = 1,
    /* The bounded guest routine returned to its supplied sentinel address. */
    MUSASHI_RESETGRAPH_PREFIX_COMPLETE = 2,
    /* The continuous startup exhausted its bounded execution budget. */
    MUSASHI_RESETGRAPH_PREFIX_STEP_LIMIT = 3
} MusashiResetGraphPrefixStatus;

typedef struct MusashiResetGraphPrefixStop {
    uint32_t call_address;
    uint32_t target_address;
} MusashiResetGraphPrefixStop;

/* Read-only observations of one continuous CPU. CP0/interrupt eligibility is
 * device-owned; these snapshots do not claim a complete BIOS CPU context. */
typedef struct MusashiEntryCpuSnapshot {
    uint32_t r[32];
    uint32_t hi, lo, pc, npc;
} MusashiEntryCpuSnapshot;
typedef void (*MusashiEntryObserver)(void *, const MusashiEntryCpuSnapshot *);
typedef struct MusashiEntryRunStop {
    MusashiResetGraphPrefixStop boundary;
    MusashiEntryCpuSnapshot cpu;
} MusashiEntryRunStop;

/* Explicit services reached by the bounded 8005FC68 CD startup sequence.
 * Selectors are intentionally named by vector/slot rather than inferred API
 * names. Each callback returns nonzero only when it accepted the operation. */
typedef struct MusashiCdDevice {
    void *userdata;
    int (*enter_critical_section)(void *userdata);
    int (*exit_critical_section)(void *userdata);
    int (*b0_5b)(void *userdata, int32_t argument, int32_t *result);
    int (*b0_4a)(void *userdata, int32_t argument, int32_t *result);
    int (*b0_4b)(void *userdata, int32_t *result);
    int (*a0_70)(void *userdata, int32_t *result);
    /* Continuous execution requires SYS1's guest return value. The legacy
     * acceptance-only callback above remains for old diagnostic wrappers. */
    int (*enter_critical_result)(void *userdata, int32_t *result);
} MusashiCdDevice;

/* Start from mapped EXE bytes, executing its source-owned CRT and caller on
 * one CPU. Native entry policy: zero GPRs/HI/LO except explicit incoming RA.
 * This policy is not a captured BIOS register seed. The observer receives
 * entry/caller/graphics/CD service checkpoints, never a mutable CPU handle.
 * CallbackDevice owns SYS2 throughout the run; CdDevice's legacy SYS2 field
 * is used only by its standalone diagnostic wrapper.
 * Unavailable services retain partial effects and return a bounded stop. */
MusashiResetGraphPrefixStatus musashi_boot_run_entry(
    MusashiBootMemory *, uint32_t incoming_ra,
    const MusashiCallbackDevice *, const MusashiCdDevice *,
    MusashiConsoleSink, void *sink_userdata,
    MusashiEntryObserver, void *observer_userdata, MusashiEntryRunStop *);
/* After MAIN.CD member0007 is resident, 800CEEC8 is that overlay's dispatcher
 * rather than member0000's 800CEE40 delay-slot nop. */
void musashi_boot_select_overlay_0004_words(int enable);
void musashi_boot_select_overlay_0007_words(int enable);
/* After MAIN.CD member0010 replaces 0007 at 800CEDF8, 800CEFDC is the
 * title overlay function 8001125C jals, not member0007 table[2]. */
void musashi_boot_select_overlay_0010_words(int enable);

/* Explicit B0 event-vector operations used by 8006189C. Handles are opaque
 * signed guest words supplied by the platform adapter and retained in guest
 * RAM; the port neither invents nor dereferences them. */
typedef struct MusashiEventDevice {
    void *userdata;
    int (*enter_critical_section)(void *userdata, int32_t *result);
    int (*open_event)(void *userdata, uint32_t class_word, uint32_t spec,
                      uint32_t mode, uint32_t callback, int32_t *handle);
    int (*enable_event)(void *userdata, int32_t handle, int32_t *result);
    int (*b0_0b)(void *userdata, int32_t handle, int32_t *result);
    int (*exit_critical_section)(void *userdata, int32_t *result);
} MusashiEventDevice;

/* Owner-thread relative source execution timeline. The selected reference
 * charges two cycles per instruction and one per data access. This excludes
 * native BIOS/intrinsic work and is not a captured or wall-clock phase.
 * Refusal must precede instruction effects; a failed run must not be retried. */
/* Inspect only the live opaque continuation supplied by a startup checkpoint.
 * The caller declares SOURCE vs an actual compiled HOST_BIOS_SERVICE scope.
 * SOURCE requires an exported instruction; HOST reports no physical PC/Cause.
 * GPR storage is borrowed from that CPU and remains valid only while suspended.
 * No exception is entered and no time advances. Output is unchanged on refusal. */
int musashi_boot_cpu_context(const void *continuation,
    MusashiCpuContextProvenance provenance, MusashiCpuContext *out);
/* Known compiled-service entry only, with its required capability present.
 * Unknown/unbound PCs cannot be relabeled as a host service. */
int musashi_boot_continuation_host_context(const void *continuation,
    MusashiCpuContext *out);

typedef struct MusashiExecutionClock {
    void *userdata;
    int (*advance)(void *, uint32_t cycles);
} MusashiExecutionClock;

typedef struct MusashiInputDevice {
    void *userdata;
    int (*enter_critical_section)(void *userdata);
    int (*c0_3)(void *userdata, int32_t argument, uint32_t buffer_address);
    int (*c0_2)(void *userdata, int32_t argument, uint32_t buffer_address);
    int (*c0_0a)(void *userdata, int32_t argument, int32_t value);
    int (*exit_critical_section)(void *userdata);
    /* Continuous source execution keeps host acceptance separate from v0. */
    int (*dequeue_irq_result)(void *, int32_t priority, uint32_t descriptor, int32_t *result);
    int (*enqueue_irq_result)(void *, int32_t priority, uint32_t descriptor, int32_t *result);
    int (*change_timer_result)(void *, int32_t channel, int32_t value, int32_t *result);
    int (*read32)(void *, uint32_t address, uint32_t *value);
    int (*write32)(void *, uint32_t address, uint32_t value);
    const MusashiExecutionClock *clock;
    /* Optional checked SIO configuration / Timer2 provider. Supplying both
     * enables only the exact audited serial child calls; further protocol
     * children remain unsupported. */
    int (*read16)(void *, uint32_t address, uint16_t *value);
    int (*write16)(void *, uint32_t address, uint16_t value);
    /* Checked SIO DATA only. Both byte callbacks additionally opt into the
     * audited metadata-preparation children; arbitrary callbacks stay refused. */
    int (*read8)(void *, uint32_t address, uint8_t *value);
    int (*write8)(void *, uint32_t address, uint8_t value);
} MusashiInputDevice;

enum {
    MUSASHI_BIOS_CALLBACK_STACK_BASE = 0x84d8u,
    MUSASHI_BIOS_CALLBACK_STACK_TOP = 0x85d8u
};
/* Dedicated native BIOS callback stack. It translates only [84D8,85D8),
 * never the game's stack or a captured low-RAM image. Fresh storage only. */
typedef struct MusashiBiosCallbackFrame {
    uint8_t scratch[MUSASHI_BIOS_CALLBACK_STACK_TOP - MUSASHI_BIOS_CALLBACK_STACK_BASE];
    int busy;
    int faulted;
} MusashiBiosCallbackFrame;
void musashi_boot_bios_callback_frame_init(MusashiBiosCallbackFrame *);
int musashi_boot_execute_input_bios_callback(MusashiBootMemory *,
    const MusashiInputDevice *, MusashiBiosCallbackFrame *, uint32_t target,
    int32_t argument, int32_t *result, MusashiResetGraphPrefixStop *stop);


/* The independent platform adapters required by the post-entry startup calls.
 * They remain distinct because their vector/service contracts are distinct. */
struct MusashiCdRegisterDevice;
struct MusashiCdSpuDevice;
/* Host acceptance is separate from guest results. Selected A0:39 InitHeap
 * preserves v0 and owns persistent allocator state plus the guest header. */
typedef struct MusashiHeapDevice {
    void *userdata;
    int (*init_heap)(void *, uint32_t base, uint32_t size);
} MusashiHeapDevice;

/* Checked GPU registers and the GPU DMA channel. A bound provider owns its
 * allowed addresses exclusively; refusal never falls back to another device. */
typedef struct MusashiGpuIoDevice {
    void *userdata;
    int (*read32)(void *, uint32_t address, uint32_t *value);
    int (*write32)(void *, uint32_t address, uint32_t value);
} MusashiGpuIoDevice;

/* CPU transfers remain provider-owned; the generic formatter never owns a
 * second CP0/GTE bank or directly links the renderer's global registers. */
typedef struct MusashiCpuTransferDevice {
    void *userdata;
    int (*read_status)(void *, const MusashiCpuContext *, uint32_t *);
    int (*write_status)(void *, const MusashiCpuContext *, uint32_t);
    int (*write_control)(void *, const MusashiCpuContext *, uint32_t, uint32_t);
    int (*write_data)(void *, const MusashiCpuContext *, uint32_t, uint32_t);
    int (*read_data)(void *, const MusashiCpuContext *, uint32_t, uint32_t *);
    int (*read_control)(void *, const MusashiCpuContext *, uint32_t, uint32_t *);
    int (*command)(void *, const MusashiCpuContext *, uint32_t);
} MusashiCpuTransferDevice;
/* The image owner reserves its exact low-RAM aliases before generic RAM. */
typedef struct MusashiBiosImageDevice {
    void *userdata;
    int (*intersects)(uint32_t, size_t);
    int (*read32)(void *, uint32_t, uint32_t *);
    int (*write32)(void *, uint32_t, uint32_t);
    int (*table)(void *, uint32_t *);
    int (*publish)(void *);
} MusashiBiosImageDevice;

typedef struct MusashiStartupPrefixDevice {
    const MusashiCallbackDevice *callback;
    const MusashiCdDevice *cd;
    const MusashiEventDevice *event;
    const MusashiInputDevice *input;
    const MusashiExecutionClock *clock;
    const struct MusashiCdRegisterDevice *cd_registers;
    const struct MusashiCdSpuDevice *cd_spu;
    const MusashiHeapDevice *heap;
    const MusashiGpuIoDevice *gpu_io;
    const MusashiCpuTransferDevice *cpu_transfer;
    const MusashiBiosImageDevice *bios_image;
} MusashiStartupPrefixDevice;

/* Continuous entry with independent event services. SYS1/SYS2 still use
 * CdDevice/CallbackDevice; EventDevice's legacy critical fields are unused.
 * The input adapter is reserved for the later supported input boundary. */
MusashiResetGraphPrefixStatus musashi_boot_run_entry_with_devices(
    MusashiBootMemory *, uint32_t incoming_ra, const MusashiStartupPrefixDevice *,
    MusashiConsoleSink, void *sink_userdata,
    MusashiEntryObserver, void *observer_userdata, MusashiEntryRunStop *);

/* Run one already-fetched source function on a nested CPU until it returns
 * to a sentinel RA. COMPLETE means that function returned; it is not a menu
 * or IRQ-success claim. Unmapped fetches publish the refused PC in stop. */
MusashiResetGraphPrefixStatus musashi_boot_execute_source_function(
    MusashiBootMemory *, const MusashiStartupPrefixDevice *, uint32_t target,
    MusashiConsoleSink, void *sink_userdata, MusashiResetGraphPrefixStop *);

/* userdata is MusashiBootMemory*. Only the eight exact 800617CC..80061858
 * five-word BIOS flag callback entry points are accepted. No checkpoint or
 * interrupted CPU is consumed; intended guest flag stores are retained. */
int musashi_boot_execute_bios_event(void *userdata, uint32_t callback);

typedef struct MusashiStartupRetryDevice {
    void *userdata;
    int (*probe_80044d38)(void *userdata, int32_t *result);
} MusashiStartupRetryDevice;

/* Supplies the callback-maintained tick word at a deliberate poll boundary. */
typedef struct MusashiTickDevice {
    void *userdata;
    int (*refresh_tick)(void *userdata, int32_t *tick);
} MusashiTickDevice;

/* Banked CD byte registers (1F801800..1F801803), plus optional CD/DMA words.
 * A callback returns
 * nonzero when it accepts an access. Responses and interrupt state belong
 * to the backend; this interface supplies no completion values. */
typedef struct MusashiCdRegisterDevice {
    void *userdata;
    int (*read8)(void *userdata, uint32_t address, uint8_t *value);
    int (*write8)(void *userdata, uint32_t address, uint8_t value);
    /* Optional checked CDROM_DELAY, COMMON_DELAY, DPCR and DMA3 word access. The same owner is
     * used by the main continuation and source CD interrupt frame. These
     * callbacks do not dispatch guest code or advance the device clock. */
    int (*read32)(void *userdata, uint32_t address, uint32_t *value);
    int (*write32)(void *userdata, uint32_t address, uint32_t value);
} MusashiCdRegisterDevice;

/* Normal game IRQ callback frame, separate from the interrupted startup CPU
 * and from the BIOS input callback stack. The installed live HookEntryInt
 * record supplies the source-owned stack. Fresh runtime storage only. */
typedef struct MusashiCdIrqFrame {
    int busy, faulted;
    uint32_t instructions;
    uint32_t budget_exhausted; /* 0 none, 1 IRQ work, 2 PAC decoder. */
} MusashiCdIrqFrame;
/* Explicit services for the bounded CD sync and first-sector ready callbacks.
 * This host result reports acceptance, never a guest BIOS return value. The
 * adapter scans its live event owner on the same thread, refuses unsupported
 * matching guest callbacks, and must not recursively dispatch IRQs. Its owner
 * and userdata must outlive the synchronous call. It may not mutate this
 * service descriptor or the active HookEntryInt context during execution. */
typedef struct MusashiCdIrqServices {
    void *userdata;
    int (*deliver_event)(void *userdata, uint32_t class_word, uint32_t spec);
    /* Same-lifetime GPUSTAT/Timer1 capability for source VSync(-1). Native
     * legacy value-returning adapters terminate on owner refusal. */
    const MusashiCallbackDevice *callback_device;
    /* Optional GP0/GP1/DMA2 owner so LoadImage from a CD ready callback can
     * submit the same source GPU words as the main formatter. */
    const MusashiGpuIoDevice *gpu_io;
    /* Optional DMA IRQ-mask/callback publisher for LoadImage's 800425E0. */
    const struct MusashiDmaCallbackRegistrationDevice *dma;
    /* Optional SPU halfword owner so PAC mode-4 voice setup (8003AB84
     * sh 0x1A6(D_8006B54C) → 1F801DA6) uses the same write16 as main. */
    const struct MusashiCdSpuDevice *cd_spu;
    /* Optional BIOS TestEvent (B0:0B) for 8003C4F0's jal 8005CEA8. */
    const MusashiEventDevice *event;
} MusashiCdIrqServices;
/* Runs checked sync43398 and normal one-sector INT1->45D00, including
 * synchronous header/payload DMA, VSync(-1), Pause and restored callbacks.
 * Async, recovery and unknown callbacks remain refused. B0:07 supports only
 * the exact F0000003/spec20 or40 source veneers. DeliverEvent has a void
 * guest ABI: v0 is preserved under the verified dead-result path, not set to
 * host acceptance or claimed to match BIOS caller-saved register residue.
 * The same live IRQ frame and checked JAL return stack are retained. */
int musashi_boot_execute_cd_irq_with_services(MusashiBootMemory *,
    const MusashiCdRegisterDevice *, const MusashiExecutionClock *,
    const MusashiCdIrqServices *, MusashiCdIrqFrame *,
    uint32_t context_address, uint32_t target, MusashiResetGraphPrefixStop *stop);
/* Compatibility wrapper: no BIOS service is bound; a non-null sync callback
 * requiring DeliverEvent remains refused. */
int musashi_boot_execute_cd_irq(MusashiBootMemory *,
    const MusashiCdRegisterDevice *, const MusashiExecutionClock *,
    MusashiCdIrqFrame *, uint32_t context_address, uint32_t target,
    MusashiResetGraphPrefixStop *stop);

/* Only the SPU halfwords used by 80044BF4 are dispatched by this boundary. */
typedef struct MusashiCdSpuDevice {
    void *userdata;
    int (*read16)(void *userdata, uint32_t address, uint16_t *value);
    int (*write16)(void *userdata, uint32_t address, uint16_t value);
} MusashiCdSpuDevice;

MusashiResetGraphPrefixStatus musashi_boot_run_80044bf4_with_device(
    MusashiBootMemory *memory, const MusashiCdRegisterDevice *cd,
    const MusashiCdSpuDevice *spu, MusashiResetGraphPrefixStop *stop);

MusashiResetGraphPrefixStatus musashi_boot_run_80044670_with_device(
    MusashiBootMemory *memory, int32_t command, uint32_t payload_address,
    const MusashiTickDevice *tick_device,
    const MusashiCdRegisterDevice *register_device,
    MusashiResetGraphPrefixStop *stop);

/* Source-backed binding of the observed 8006189C event registration sequence.
 * The complete RAM state and supplied adapter must be accepted before writes.
 * This is a bounded registration operation, not event delivery. */
int musashi_boot_call_8006189c(MusashiBootMemory *memory,
                               const MusashiEventDevice *device,
                               int32_t *result);

/* Execute the exact ResetGraph 80059234 stream until its first unresolved
 * callback-initializer boundary. No callback, BIOS vector, or device result is
 * fabricated. Mode 5 reaches 80042580 after the exact formatter/clear
 * sequence, follows its guest callback-table indirection, then stops at the
 * selected initializer; mode 0 additionally requires loaded formatter data. */
MusashiResetGraphPrefixStatus musashi_boot_run_resetgraph_prefix(
    MusashiBootMemory *memory, int32_t mode, MusashiConsoleSink sink,
    void *userdata, int32_t *result, MusashiResetGraphPrefixStop *stop);

/* Execute the exact 800141F0 startup body until the same callback-initializer
 * boundary. This starts at the actual startup call rather than invoking
 * ResetGraph directly, but remains a bounded diagnostic: later 800141F0 calls
 * are not reached until 80042718 has an explicit native contract. */
MusashiResetGraphPrefixStatus musashi_boot_run_800141f0_prefix(
    MusashiBootMemory *memory, MusashiConsoleSink sink, void *userdata,
    int32_t *result, MusashiResetGraphPrefixStop *stop);

/* Follow the exact 800141F0 path through ResetGraph and callback initialization
 * using explicit platform callbacks. COMPLETE means this bounded guest routine
 * returned to its sentinel address; it is not a successful game startup. */
MusashiResetGraphPrefixStatus musashi_boot_run_800141f0_callback_prefix(
    MusashiBootMemory *memory, const MusashiCallbackDevice *device,
    MusashiConsoleSink sink, void *userdata, int32_t *result,
    MusashiResetGraphPrefixStop *stop);

/* Execute the exact 8005FC68 CD-startup call under explicit vector/syscall
 * adapters. COMPLETE means this bounded routine returned, not that CD I/O or
 * game startup succeeded. The callback-table word at 80072A24 remains guest
 * RAM and determines B0:4A's observed argument. */
MusashiResetGraphPrefixStatus musashi_boot_run_8005fc68_prefix(
    MusashiBootMemory *memory, const MusashiCdDevice *device, int32_t *result,
    MusashiResetGraphPrefixStop *stop);

/* Execute exact 8005FCB8 state setup through 80062888, then stop before the
 * event-registration routine 8006189C. No event handles or callback-table
 * dispatch are synthesized by this diagnostic. */
MusashiResetGraphPrefixStatus musashi_boot_run_8005fcb8_prefix(
    MusashiBootMemory *memory, MusashiResetGraphPrefixStop *stop);

/* Continue the same exact 8005FCB8 trace through source-backed event
 * registration and callback-table dispatch. COMPLETE means this bounded guest
 * routine returned; it does not establish game startup or event delivery. */
MusashiResetGraphPrefixStatus musashi_boot_run_8005fcb8_event_prefix(
    MusashiBootMemory *memory, const MusashiEventDevice *device,
    int32_t *result, MusashiResetGraphPrefixStop *stop);

/* Execute the exact startup-record setup at 80018918 through its two record
 * clears, post-record state setup, already-initialized callback wrapper, and
 * completed 8005F0C8 startup routine. Stops before input startup at 8005D8B4;
 * no controller service behavior is synthesized. */
MusashiResetGraphPrefixStatus musashi_boot_run_80018918_prefix(
    MusashiBootMemory *memory, MusashiResetGraphPrefixStop *stop);

/* Continue the same exact 80018918 path through 8005D0F8 and completed
 * 8005D8B4 input startup using only the explicit selector adapter. COMPLETE
 * is a bounded return to the supplied sentinel, not a boot or menu claim. */
MusashiResetGraphPrefixStatus musashi_boot_run_80018918_input_prefix(
    MusashiBootMemory *memory, const MusashiInputDevice *device,
    MusashiResetGraphPrefixStop *stop);

/* Follow the direct retail startup calls after the CRT/exchange prefix:
 * 800141F0, 8005FC68, 8005FCB8, and 80018918. The caller must already have
 * performed the exact entry prefix through 800101C0. This driver stops before
 * 80043300 at its genuine 800101E4 call site; it does not invoke its retry or
 * event behavior. */
MusashiResetGraphPrefixStatus musashi_boot_run_post_entry_prefix(
    MusashiBootMemory *memory, const MusashiStartupPrefixDevice *device,
    MusashiConsoleSink sink, void *userdata, int32_t *result,
    MusashiResetGraphPrefixStop *stop);

/* Continue the direct retail call at 800101E4 after the bounded post-entry
 * driver reaches it. COMPLETE means the 80043300 dispatcher returned under
 * the supplied retry adapter; it does not establish callbacks, CD I/O, native
 * boot, or menu output. */
MusashiResetGraphPrefixStatus musashi_boot_run_post_entry_dispatch_prefix(
    MusashiBootMemory *memory, const MusashiStartupPrefixDevice *device,
    const MusashiStartupRetryDevice *retry_device, MusashiConsoleSink sink,
    void *userdata, int32_t *result, MusashiResetGraphPrefixStop *stop);
MusashiResetGraphPrefixStatus musashi_boot_run_80043450_prefix(
    MusashiBootMemory *memory, const MusashiStartupRetryDevice *device,
    int32_t argument, int32_t *result, MusashiResetGraphPrefixStop *stop);

/* Executes retail 80044D38 until its first platform/dependency crossing. */
MusashiResetGraphPrefixStatus musashi_boot_run_80044d38_prefix(
    MusashiBootMemory *memory, MusashiResetGraphPrefixStop *stop);

/* Same prefix after composing only its first, matched console-string call. */
MusashiResetGraphPrefixStatus musashi_boot_run_80044d38_console_prefix(
    MusashiBootMemory *memory, MusashiConsoleSink sink, void *userdata,
    MusashiResetGraphPrefixStop *stop);

/* Same prefix after composing its matched string and formatter calls. */
MusashiResetGraphPrefixStatus musashi_boot_run_80044d38_console_format_prefix(
    MusashiBootMemory *memory, MusashiConsoleSink sink, void *userdata,
    MusashiResetGraphPrefixStop *stop);

/* Same prefix through callback initialization; CD command submission stays
 * an explicit boundary. */
MusashiResetGraphPrefixStatus musashi_boot_run_80044d38_callback_prefix(
    MusashiBootMemory *memory, const MusashiCallbackDevice *device,
    MusashiConsoleSink sink, void *userdata, MusashiResetGraphPrefixStop *stop);

/* Executes exact 80044670 setup through its first diagnostic branch. */
MusashiResetGraphPrefixStatus musashi_boot_run_80044670_prefix(
    MusashiBootMemory *memory, int32_t command, uint32_t payload_address,
    MusashiResetGraphPrefixStop *stop);
MusashiResetGraphPrefixStatus musashi_boot_run_80044670_with_tick(
    MusashiBootMemory *memory, int32_t command, uint32_t payload_address,
    const MusashiTickDevice *device, MusashiResetGraphPrefixStop *stop);

/* Execute the exact 80044DBC initializer through its 80044670 command call. */
MusashiResetGraphPrefixStatus musashi_boot_run_80044dbc_command(
    MusashiBootMemory *memory, uint32_t out_value, uint32_t out_address,
    uint32_t call_args[4], MusashiResetGraphPrefixStop *stop);

/* Execute the exact 8004239C(-1) guest-state tick query. */
MusashiResetGraphPrefixStatus musashi_boot_run_8004239c_minus_one(
    MusashiBootMemory *memory, int32_t *result, MusashiResetGraphPrefixStop *stop);

/* Execute the first 80044124 status-poll iteration through its tick setup. */
MusashiResetGraphPrefixStatus musashi_boot_run_80044124_first_poll(
    MusashiBootMemory *memory, MusashiResetGraphPrefixStop *stop);
MusashiResetGraphPrefixStatus musashi_boot_run_80044124_poll_with_tick(
    MusashiBootMemory *memory, const MusashiTickDevice *device,
    MusashiResetGraphPrefixStop *stop);
/* Continue the same first-poll frame through the timeout diagnostic's console,
 * formatter, CD-state-reset, and return. This does not implement command
 * completion or the start menu. */
MusashiResetGraphPrefixStatus musashi_boot_run_80044124_poll_with_tick_console(
    MusashiBootMemory *memory, const MusashiTickDevice *device,
    MusashiConsoleSink sink, void *userdata, MusashiResetGraphPrefixStop *stop);

/* Execute the exact post-poll deadline comparison for a supplied guest tick. */
MusashiResetGraphPrefixStatus musashi_boot_run_80044198_timeout_prefix(
    MusashiBootMemory *memory, int32_t tick, MusashiResetGraphPrefixStop *stop);

/* Execute the exact 80043B9C CD-status worker until its return or first
 * external formatter call. This assembly-sourced bridge is not a C decomp. */
MusashiResetGraphPrefixStatus musashi_boot_run_80043b9c_prefix(
    MusashiBootMemory *memory, MusashiResetGraphPrefixStop *stop);
MusashiResetGraphPrefixStatus musashi_boot_run_80043b9c_console_prefix(
    MusashiBootMemory *memory, MusashiConsoleSink sink, void *userdata,
    MusashiResetGraphPrefixStop *stop);

MusashiResetGraphPrefixStatus musashi_boot_run_80044b14_prefix(
    MusashiBootMemory *memory, MusashiResetGraphPrefixStop *stop);
MusashiResetGraphPrefixStatus musashi_boot_run_800434bc_prefix(
    MusashiBootMemory *memory, MusashiResetGraphPrefixStop *stop);
MusashiResetGraphPrefixStatus musashi_boot_run_80043300_prefix(
    MusashiBootMemory *memory, const MusashiStartupRetryDevice *device,
    MusashiResetGraphPrefixStop *stop);
MusashiResetGraphPrefixStatus musashi_boot_run_80043300_console_prefix(
    MusashiBootMemory *memory, const MusashiStartupRetryDevice *device,
    MusashiConsoleSink sink, void *userdata, int32_t *result,
    MusashiResetGraphPrefixStop *stop);

/* Execute the exact 8005F0C8 startup prefix with the two guest ranges
 * supplied by 80018918, including its 8005F728 callback-table setup and
 * 8005D6A0 table initializer. COMPLETE means this bounded guest routine
 * returned; it does not establish allocator or game-startup completion. */
MusashiResetGraphPrefixStatus musashi_boot_run_8005f0c8_prefix(
    MusashiBootMemory *memory, MusashiResetGraphPrefixStop *stop);
MusashiResetGraphPrefixStatus musashi_boot_run_8005d8b4_prefix(
    MusashiBootMemory *memory, const MusashiInputDevice *device,
    MusashiResetGraphPrefixStop *stop);

typedef enum MusashiCallbackInitPrefixStatus {
    MUSASHI_CALLBACK_INIT_PREFIX_INVALID_INPUT = 0,
    MUSASHI_CALLBACK_INIT_PREFIX_BIOS_BOUNDARY = 1,
    MUSASHI_CALLBACK_INIT_PREFIX_INSTALLER_BOUNDARY = 2,
    MUSASHI_CALLBACK_INIT_PREFIX_CHANNEL_INSTALLER_BOUNDARY = 3,
    MUSASHI_CALLBACK_INIT_PREFIX_A0_72_BOUNDARY = 4,
    MUSASHI_CALLBACK_INIT_PREFIX_EXIT_CRITICAL_BOUNDARY = 5,
    MUSASHI_CALLBACK_INIT_PREFIX_RETURN_BOUNDARY = 6
} MusashiCallbackInitPrefixStatus;

/* Execute exact callback initialization from 80042718 through its interrupt
 * and DMA setup, state clear, and saved-register record. The explicitly
 * supplied adapter owns the three observed device registers. Without
 * hook_entry_int, execution stops before B0:19 at 8005CEF8. With an adapter
 * that accepts the observed 8006BB34 context, execution continues through
 * that explicit host registration and stops before 80042C90. With the
 * additional registration_device, it executes the exact 80042C90 installer
 * and 80042DB8 channel installer through shared 800425B0/800429DC C, then
 * stops before A0:72. Registration state is owned by that recovered code;
 * the provider supplies checked IRQ accesses and B0:5B/C0:0A services.
 * Optional a0_72 and exit_critical_section adapters advance through those
 * explicit services and stop at the initializer's final return-transfer
 * boundary. Neither path emulates callback delivery or treats initialization
 * as complete. */
MusashiCallbackInitPrefixStatus musashi_boot_run_callback_init_prefix(
    MusashiBootMemory *memory, const MusashiCallbackDevice *device,
    MusashiResetGraphPrefixStop *stop);

typedef enum MusashiPrefixStatus {
    MUSASHI_PREFIX_INVALID_INPUT = 0,
    MUSASHI_PREFIX_UNSUPPORTED_CALL = 1
} MusashiPrefixStatus;

typedef struct MusashiPrefixStop {
    uint32_t call_address;
    uint32_t target_address;
} MusashiPrefixStop;

/* Bounded native diagnostic for the byte-checked 80010190..800101C0 prefix:
 * CRT once-guard (the pinned EXE has zero constructors), decomp-owned 80043060,
 * then the 80074778 write. Stops BEFORE unsupported 800141F0, without a stub.
 * This is not the complete startup routine and has no successful-boot status.
 * Caller supplies loaded/initialized, exclusively owned RAM. stop must not
 * overlap it. Null inputs leave RAM and stop unchanged. Repeated invocation
 * reruns the prefix, not a continuation from the previous stop. */
MusashiPrefixStatus musashi_boot_run_prefix(MusashiBootMemory *memory,
                                           MusashiPrefixStop *stop);

#endif
