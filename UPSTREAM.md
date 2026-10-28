# Upstream software and changes

The source trees of **vbcc**, **vasm**, and **vlink** belong to their original authors, primarily Volker Barthelmann and Frank Wille. This repository is a separate 78K/3 backend distribution, **not a replacement or relicensed fork** of their original code.

| Component | Upstream source changes required |
| --- | --- |
| [vbcc](https://www.compilers.de/vbcc.html) | **None.** New directory `machines/78k3/` only |
| [vasm](https://sun.hasenbraten.de/vasm/) | New directory `cpus/78k3/`, plus a small `syntax/std/syntax.c` patch |
| [vlink](https://sun.hasenbraten.de/vlink/) | **None.** Built directly from the original archive |

## Why vasm needs a patch

`patches/vasm-std-78k3.patch` touches exactly one upstream file, `syntax/std/syntax.c`, for two unrelated reasons:

- The standard syntax parser already treats the apostrophe in Z80 alternate register names (for example `AF'`) as part of the operand instead of the start of a character literal. The 78K/3 also has primed alternate register names (`X'`, `AX'`, `BC'`, etc.), so the patch extends that existing Z80 treatment to `VASM_CPU_78K3`.
- After an instruction's operands are parsed, the patch calls `cpu_fixup_bit_syntax()` (defined in `cpus/78k3/cpu.c`) when `VASM_CPU_78K3` is defined. The 78K/3 bit-manipulation instructions (`SET1`, `CLR1`, `AND1`, `OR1`, `XOR1`, `MOV1`, `NOT1`, and the `BT`/`BF`/`BTCLR` branch-on-bit forms) take a `byte.bit` operand (`sfr[EXIC0].7`, `flags.3`, and the like); this hook is what lets the standard syntax module recognize that operand shape and hand it to the backend, rather than something the backend alone can add.

Both changes are scoped to standard syntax operand scanning/parsing; the upstream maintainers may suggest a backend-only alternative for either when reviewing this change.

A separate `vasm/Makefile.78k3` provides the assembler build configuration without modifying upstream build rules. The backend's own version is defined in `cpu.c`. The distribution therefore has **just one patched file**, even though it serves two purposes.

The assembler-specific Makefile uses ISO C99. Shared opcode bytes and masks are written in hexadecimal, with binary comments for significant bitfields; no GNU binary integer literals are required.

## Provenance and release versions

The repository includes these original, unmodified source archives with descriptive release identifiers. Project names link to their official upstream websites; archive filenames link to the copies included in this repository:

| Project | Release / snapshot | File | SHA-256 |
| --- | --- | --- | --- |
| [vbcc](https://www.compilers.de/vbcc.html) | 2026-01-10 | [`vbcc-10_JAN_2026.tar.gz`](upstream/vbcc-10_JAN_2026.tar.gz) | `67e221062b50fb590613487ec2948a00db80552e061bbe348e6eaed5b33f0af8` |
| [vasm](https://sun.hasenbraten.de/vasm/) | v2.0f | [`vasm-v2.0f.tar.gz`](upstream/vasm-v2.0f.tar.gz) | `c84b2de1cbb87831795fe64a85c5d9a7002a766e3a7c30b0a2d7d5e99d878f49` |
| [vlink](https://sun.hasenbraten.de/vlink/) | v0.18a | [`vlink-v0.18a.tar.gz`](upstream/vlink-v0.18a.tar.gz) | `8d151cdd30a4feb575a364e68810c2bc300fe1a7c074dbbee6fd1175a6c5bfae` |

The version-qualified filenames are labels; the archive contents should not be repacked or altered. The build script validates SHA-256 checksums against `upstream/SHA256SUMS` and aborts if any differ. Compatibility with other upstream releases is not assumed.

Original upstream licenses also govern any portions derived from original templates or source, and any compiled binaries linked with the original toolchain. The license for the separately authored additions is in [LICENSE-78K3.txt](LICENSE-78K3.txt). Permission to distribute the separately authored backend with unmodified original sources was discussed directly with the upstream maintainer.

Development of these backends involved substantial AI assistance. Source review, hardware testing, and compatibility remain ongoing.
