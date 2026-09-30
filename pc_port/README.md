# Native PC port smoke target

This directory is a platform adapter seam for the decompilation project. It
does not contain gameplay code, retail assets, or an emulator. The smoke target
only proves that a native build can consume the decomp-owned C API in
`src/shared/runtime_api.[hc]` through a narrow adapter.

The shared contract currently consists of:

- `musashi_runtime_reset(MusashiRuntimeState *)`
- `musashi_read_le32(const mus_u8[4])`

`musashi_pc_port` forwards those calls and selects a platform name from
`platform/linux.c` or `platform/windows.c`. This is build/link/runtime plumbing
only; no gameplay portability or byte-for-byte behavioral equivalence is
claimed by this target.

## Linux smoke run

From the repository root:

```sh
cmake -S . -B build/linux -DMUSASHI_BUILD_PC_PORT=ON
cmake --build build/linux
ctest --test-dir build/linux --output-on-failure
./build/linux/musashi_pc_smoke
```

Windows is source/configuration readiness only: validate the same CMake target
with a Windows generator or cross-compiler, but do not interpret that result as
gameplay portability. The adapter intentionally avoids platform-specific
libraries, so the source remains dependency-free.
