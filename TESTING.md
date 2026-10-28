# Testing the 78K/3 toolchain

This is **development-stage software**. A successful build, assembly, or C compilation **does not prove that generated machine code is correct**.

The original source archives are already included. Run `make test` from the repository root to build the tools and execute these smoke tests. They compile C to assembly, assemble object files, resolve a cross-file symbol with vlink, and check the NOP instruction's expected byte:

```sh
make test
# Outputs include build/nop.bin (00), build/smoke.asm, and build/link.bin
```

(vbcc's own flags take the form `-name=value`, not a separate `-o
<file>` argument, and it has no `-S`: unlike gcc, vbcc only ever emits
assembly, so there's no flag to ask for that.)

These are **smoke tests**, not comprehensive acceptance tests. The image
`build/link.bin` uses a deliberately minimal linker script (`tests/link.ld`)
and **must not be flashed to a radio**: it has no valid radio vectors,
startup code, watchdog handling, or required image layout. The linker test
only checks that vasm and vlink can resolve a symbol across object files.

Command-line behavior and supported operations may still evolve; report
failures with the output and exact archive versions.

## Testing generated C on real hardware

To verify end-to-end execution on a Kenwood TK-840 development radio, use
the existing tested startup, mask-ROM service shims, and linker scripts
in [Kenwood TK-x4x Reverse Engineering](https://github.com/KY4WI/Kenwood_TK-x4x_Reverse_Engineering/tree/main/tools/probes).
For example, from that repository's `tools/probes` directory, set `VBCC`,
`VASM`, and `VLINK` to the absolute paths of the executables from this
repository's `bin/` and run `./panelprobe/build.sh`.

That produces a correctly sized radio image using the existing probe's
startup and memory map. Build it, compare with the known-good image, and
test on your bench radio using your established flashing/recovery workflow.
An ordinary `tests/smoke.c` compile alone does **not** create a flashable
image. Live hardware validation must be done separately; no hardware
test is implied by `make test`.

For meaningful verification, compare emitted bytes against the NEC 78K/III instruction reference and independently known-good instruction encodings, then test calling conventions, signed/unsigned arithmetic, pointer accesses, stack frames, branches, and function arguments. Validate hardware-specific memory maps and linker scripts separately.

If you find a bug, [open an issue](https://github.com/KY4WI/NEC_78K3_Toolchain/issues) with a short reproducible input and expected/actual results. Do not upload copyrighted Kenwood firmware.
