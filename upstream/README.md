# Original toolchain source archives

This directory contains the **unmodified upstream source archives** for the specific versions used by this NEC 78K/3 build:

```text
vbcc-10_JAN_2026.tar.gz    vbcc source snapshot from 10 JAN 2026
vasm-v2.0f.tar.gz          vasm release 2.0f
vlink-v0.18a.tar.gz        vlink release 0.18a
SHA256SUMS                 SHA-256 checksums for the above archives
```

The version labels make the files identifiable. **Only the filenames have been changed, not the archive contents.** Do not repack or edit upstream source archives.

From the repository root, verify the supplied archives:

```sh
sha256sum -c upstream/SHA256SUMS
```

`./build.sh` performs this verification automatically, then extracts the sources into `build/`, installs the separate 78K/3 backends, applies the small vasm parser patch, and compiles the tools. The originals in this directory are never changed.

See [UPSTREAM.md](../UPSTREAM.md) for exact hashes, source details, patch rationale, and licensing. Upstream project sites: [vbcc](https://www.compilers.de/vbcc.html), [vasm](https://sun.hasenbraten.de/vasm/), [vlink](https://sun.hasenbraten.de/vlink/).
