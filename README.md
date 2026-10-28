# NEC 78K/3 C Toolchain

**NEC 78K/III compiler and assembler backends for vbcc and vasm**, with the original vlink linker. Intended for software development targeting the NEC 78K/III microcontroller family, including the µPD78312.

> [!WARNING]
> **Experimental / under development.** This is not a finished or fully validated C toolchain. The vbcc backend generates code, but features, code generation and ABI behavior still need testing. **Inspect and verify generated code before using it on real hardware.** If you encounter a problem, please [file a bug report](https://github.com/KY4WI/NEC-78K3-Toolchain/issues) with a minimal reproducible example.

## What this repository contains

| Component | Contents |
| --- | --- |
| `vbcc/machines/78k3/` | New NEC 78K/3 C compiler backend |
| `vbcc/machines/78k3/generated/` | Pre-generated `dt.h`/`dt.c` (see its own README) -- lets `build.sh` skip vbcc's interactive `dtgen` step |
| `vasm/cpus/78k3/` | New NEC 78K/3 assembler backend and opcode tables |
| `vasm/Makefile.78k3` | Additional 78K/3 assembler build configuration |
| `patches/vasm-std-78k3.patch` | vasm parser change: alternate registers (`AX'`, `BC'`, etc.) and `byte.bit` operand syntax (`SET1`/`CLR1`/etc.) |
| `build.sh` | Extracts original vendor archives, installs the backends, applies the patch, and builds the three tools |
| `upstream/` | Original **unmodified** vendor source archives and their checksums |

The repository **includes the unmodified original source archives** used by this build:

| Tool | Release or snapshot | Source archive |
| --- | --- | --- |
| vbcc | 10 JAN 2026 | `upstream/vbcc-10_JAN_2026.tar.gz` |
| vasm | v2.0f | `upstream/vasm-v2.0f.tar.gz` |
| vlink | v0.18a | `upstream/vlink-v0.18a.tar.gz` |

Their SHA-256 values are recorded in [upstream/SHA256SUMS](upstream/SHA256SUMS). The archive bytes are preserved; version labels in the filenames are for clarity.

The compiler itself needs **no upstream source changes**. The assembler requires only the small, separately supplied syntax patch. **vlink is unmodified.**

## Build (Linux / GCC)

You need Bash, GCC, GNU Make, `patch`, `tar`, and standard Unix utilities.

1. Clone the repository and enter its directory:

```sh
git clone https://github.com/KY4WI/NEC-78K3-Toolchain.git
cd NEC-78K3-Toolchain
```

2. Build the complete toolchain (the original upstream archives are already included):

```sh
make
```

The build uses GNU Make's native parallel job control, defaulting to one job per available logical CPU. Use `make -j4` to limit compilation to four jobs or `make -j1` for a serial build. These settings carry through to the upstream builds. When running `./build.sh` directly, you can override its CPU-count default with `MAKEFLAGS=-j4 ./build.sh`.

The top-level Makefile provides `make clean` to remove generated `build/` and `bin/` directories, `make rebuild` for a fresh build, `make verify` to check the source archive SHA-256 hashes, `make test` to exercise C compilation, assembly and linking, and `make help` for the command list. You can also invoke `./build.sh` directly.

3. Find the built executables under `bin/`:

```text
bin/vbcc78k3
bin/vc
bin/vasm78k3_std
bin/vlink
```

The build script verifies `upstream/SHA256SUMS` before extracting, copies the backends into **temporary source trees**, applies the vasm patch, and builds the programs. Extraction and compilation occur under `build/`; original upstream archives remain untouched. Run `make clean` to remove the generated files.

This is a **source build**, not an installer or a fully configured `vc` driver environment. Additional target-specific startup code, runtime support, linker scripts, and configuration may be necessary for a complete firmware image.

## Status, testing and limitations

The 78K/3 backends have been used to generate code, but are not feature-complete. The assembler's opcode tables use standard C99 hexadecimal constants; binary comments retain useful field and mask layouts. A successful build or assembly does not guarantee correct output. Consult [backend known issues](vbcc/machines/78k3/BUGS.md), [testing notes](TESTING.md), and the comments in the backend sources. Support is aimed at the µPD78312-compatible subset currently being reverse engineered; compatibility across all 78K/III devices is **not** implied. The smoke-test linked output is not a bootable radio image; see [testing notes](TESTING.md) for a real-hardware test path.

The assembler instruction tables under `vasm/cpus/78k3/shared/` are self-contained copies synchronized with the separate reverse-engineering/disassembler project. No access to that project is required to build this repository.

**Please report bugs.** Open an [issue](https://github.com/KY4WI/NEC-78K3-Toolchain/issues), including source input, the command line, expected behavior, actual output or diagnostic, host OS, and versions/checksums of the three upstream archives. A short failing case is more useful than a full firmware dump. Please **do not attach copyrighted Kenwood firmware or ROM images**.

## Original projects and permissions

vbcc, vasm and vlink are separate tools developed by Volker Barthelmann and Frank Wille. They are **not MIT/GPL open-source projects simply because their source is available**. Their original licenses continue to apply. Original archives must be unmodified; this repository distributes separate backend additions and one explicit patch. See [UPSTREAM.md](UPSTREAM.md) and [LICENSE-78K3.txt](LICENSE-78K3.txt).

This project was developed with substantial **AI assistance**, including implementation, investigation, analysis and debugging. That fact does not substitute for independent testing or review.

**Backend contributions:** Dave (KY4WI). Source-code attribution is appreciated; no prominent binary or marketing credit is requested.
