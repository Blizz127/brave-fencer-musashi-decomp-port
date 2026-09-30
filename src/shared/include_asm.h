#ifndef MUSASHI_INCLUDE_ASM_H
#define MUSASHI_INCLUDE_ASM_H

/* Portable declaration helpers used while matching MIPS routines. */
#if defined(__GNUC__) || defined(__clang__)
#define MUSASHI_ASM_LABEL(name) __asm__(#name)
#define MUSASHI_USED __attribute__((used))
#define MUSASHI_PACKED __attribute__((packed))
#else
#define MUSASHI_ASM_LABEL(name)
#define MUSASHI_USED
#define MUSASHI_PACKED
#endif

#define MUSASHI_CONCAT_INNER(a, b) a##b
#define MUSASHI_CONCAT(a, b) MUSASHI_CONCAT_INNER(a, b)

#endif
