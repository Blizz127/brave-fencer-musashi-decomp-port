/* Single-opcode GTE macros for C sources.
 *
 * Each macro emits exactly one coprocessor-2 instruction (or the one-word
 * `nop` the GTE needs between loading an input register and starting an
 * operation). Mnemonics such as avsz3 come from gte_macros.inc, which the
 * assembler expands; no source file carries instruction words itself. No
 * register is pinned: the compiler chooses every general register.
 */
#ifndef GTE_OPS_H
#define GTE_OPS_H

__asm__(".include \"gte_macros.inc\"\n");

#define gte_lwc2(r, off, p) __asm__ volatile("lwc2 $" #r ", " #off "(%0)" : : "r"(p))
#define gte_swc2(r, off, p) __asm__ volatile("swc2 $" #r ", " #off "(%0)" : : "r"(p) : "memory")
#define gte_mtc2(v, r) __asm__ volatile("mtc2 %0, $" #r : : "r"(v))
#define gte_mfc2(v, r) __asm__ volatile("mfc2 %0, $" #r : "=r"(v))
#define gte_cfc2(v, r) __asm__ volatile("cfc2 %0, $" #r : "=r"(v))
#define gte_nop() __asm__ volatile("nop")

#define gte_rtps() __asm__ volatile("rtps")
#define gte_rtpt() __asm__ volatile("rtpt")
#define gte_nclip() __asm__ volatile("nclip")
#define gte_avsz3() __asm__ volatile("avsz3")
#define gte_avsz4() __asm__ volatile("avsz4")
#define gte_dpcl() __asm__ volatile("dpcl")
#define gte_dpct() __asm__ volatile("dpct")
#define gte_intpl() __asm__ volatile("intpl")
#define gte_sqr12() __asm__ volatile("sqr 1")
#define gte_sqr0() __asm__ volatile("sqr 0")

#endif
