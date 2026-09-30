# CD device integration findings

Inspected 2026-09-05. Native menu boot remains unverified.

## Retail interface

The six words at EXE offset `5D74C` (guest `8006CF4C`) are:

| Guest pointer cell | Device address | Required access |
| --- | --- | --- |
| `8006CF4C` | `1F801800` | byte CD index/status |
| `8006CF50` | `1F801801` | byte banked command/response |
| `8006CF54` | `1F801802` | byte banked parameter/control |
| `8006CF58` | `1F801803` | byte banked interrupt/control |
| `8006CF5C` | `1F801020` | word delay configuration |
| `8006CF60` | `1F801C00` | halfword SPU register base |

Verified with `od` against `extracted/disc/files/SLUS_007.26`. Pointer cells
must continue to be read from guest memory. The RAM-only fixtures use other
addresses and cannot establish device behavior.

`80044D38` issues command bytes `01`, `0A`, and `0C` through `80044670`.
The `80044BF4` setup leaf additionally touches SPU offsets `1B8`, `1BA`,
`180`, `182`, `1B0`, `1B2`, and `1AA`, then banked CD audio-volume controls.
Thus a byte-command sink alone cannot complete initialization.

## Existing backend gap

The pinned `tools/third_party/psycross/src/psx/LIBCD.C` implements selected
high-level file/sector operations. `CdControl`, `CdControlB`, and `CdControlF`
leave startup commands `01`, `0A`, and `0C` in their unhandled branches.
They do not expose the banked register interface consumed by this guest code.
Forwarding the native register write to these functions would not implement
the required response FIFO or interrupt acknowledgement.

The locally inspected PCSX-Redux `src/core/cdrom.cc` illustrates the missing
state: index-selected writes, a consuming response FIFO, queued command
processing, and interrupt delivery. Its emulator-owned implementation is
reference material, not a linked backend or native-port acceptance proof.

## Implementation sequence

1. Add checked device accesses separate from guest RAM: CD bytes, the delay
   word, and the exact SPU halfword accesses above. Unhandled addresses fail.
2. Implement bank selection, parameter/response FIFOs, command processing,
   interrupt flags, and acknowledgement with source-backed timing. Keep this
   device state separate from the game's callback words and status bytes.
3. Connect device events to the recovered callback path. The guest worker
   must consume responses and update its own state; the host must not write
   completion values directly into `8006CF64` to advance startup.
4. Exercise initialization using the loaded retail pointer cells, then bind
   disc reads and compare the resulting startup execution with retail traces.

`MusashiCdRegisterDevice` now supplies explicit byte callbacks for
`1F801800..1F801803` through `musashi_boot_run_80044670_with_device`.
Missing callbacks, rejected accesses, and addresses outside that aperture
fail. Existing RAM-only entry points retain their previous behavior.

Tests use real CD register addresses with an explicit test backend: rejection
stops at `8004482C`; acceptance reaches the next tick query, where the fixture
deliberately stops. An adjacent invalid address never reaches the callback.
The callbacks do not themselves implement command responses, interrupts,
delay configuration, or SPU behavior. Those remain the next implementation
work, and no device completion or menu boot is claimed.

The command integration fixture now also supplies one response byte through
the register callbacks after command submission. The guest worker drains
that byte, acknowledges the interrupt, copies the response into `800763D8`,
and updates `8006CF64` itself. The host does not set completion after the
command. Execution then returns through the command epilogue. This exposed
the missing `xori` instruction at `80044A58`; the executor now supports it.
The fixture is a deterministic test backend, not a production CD controller
or evidence of hardware timing fidelity.

## Startup controller subset

`pc_port/cd_controller.c` now provides persistent controller state for command
bytes `01`, `0A`, and `0C`, with a consuming single-byte response, banked IRQ
mask/acknowledgement, busy status, and explicit monotonic cycle advancement.
It rejects unsupported commands and register operations. Drive status is
supplied at initialization; media detection is not yet implemented.

The scheduling model follows the inspected PCSX-Redux `cdrom.cc`: first
response after `0x800` cycles; command `0A` schedules a second completion
after `4100000` more cycles. An outstanding interrupt prevents delivery of
the next response. These are reference-model timings, not verified retail
timings. PCSX names `0A` CdlReset and `1C` CdlInit; do not confuse those
values when implementing the startup sequence.

The native probe checks delayed delivery, interrupt masking/acknowledgement,
response consumption, two-stage completion, demute, invalid addresses,
unsupported commands, and backwards time rejection. The controller is linked
into the native library but is not yet wired to the startup driver's tick
and interrupt adapters. Parameter/data FIFOs, media state changes, audio
volume, SPU and delay-register operations remain incomplete.

Follow-up: bank-2/bank-3 CD volume writes are now implemented as four staged
coefficients (LL, LR, RL, RR). Bank 3 register 3 bit `20` copies staged
values to active values. Tests use four distinct coefficients, verify that
writing without the apply bit leaves active values unchanged, and confirm
command/interrupt state survives volume programming. This implements the
register latch only; audio mixing and the separate SPU accesses remain open.

The native command probe now connects this controller to the recovered
`80044670` executor for `01 -> 0A -> 0C` without changing guest completion
state between commands. After `0A` acknowledgement, it verifies guest state
3 and the controller's pending second phase. The `0C` call waits through
the preceding completion and then consumes its own acknowledgement, ending
with guest state 2 and no pending controller response/interrupt. The fixture
uses the retail command-table word at `8006CD74` and interrupt jump targets.

The integration scheduler advances 8192 cycles per tick query and supplies
a deterministic tick value. This establishes command/controller interaction,
not runtime scheduling accuracy. The full initializer still needs its delay
register, SPU, callback, console, and entry-state integration.

`musashi_boot_run_80044bf4_with_device` now executes the recovered setup
leaf with the CD controller and a separate checked SPU halfword adapter.
Only reads at `1F801DB8/DBA` and writes at `1F801D80/D82/DB0/DB2/DAA`
are dispatched. The integration test supplies zero SPU read values, observes
five writes, and verifies active CD volumes `80,00,00,80`. A rejected first
SPU write stops at `80044C20`. This establishes the device boundary and
guest execution; the test SPU adapter does not implement sound hardware.
