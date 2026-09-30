# Compiler identification contract

Byte-for-byte matching depends on identifying the exact code-generation
pipeline. Do not label a toolchain as canonical from a plausible string or a
successful build alone.

Record, with evidence, at least:

- compiler family and exact version/build;
- assembler and linker versions;
- target CPU/ABI and floating-point mode;
- optimization, scheduling, section, alignment, and debug flags;
- link script, library revisions, and post-link padding/relocation steps;
- a reproducible command line and the hash of every tool input that affects
  the executable.

The first authority is the observed `PS-X EXE` identity in
`provenance/exe_identity.json`. Compiler conclusions must cite disassembly or
linker fingerprints from that executable and remain marked `unknown` until
the retail bytes have been extracted. Never replace an unresolved value with
the host compiler version.

The eventual compiler lock should include a container or immutable toolchain
pin, a clean rebuild command, and a byte comparison report. A matching hash
must be reported separately from emulator and human-visible runtime proof.
