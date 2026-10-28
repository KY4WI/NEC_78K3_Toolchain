# Pre-generated `dt.h` / `dt.c`

vbcc's own build process normally generates these two files by running
`bin/dtgen machines/78k3/machine.dt objects/78k3/dt.h objects/78k3/dt.c`
(see `make.rules` in the vbcc source, unmodified). `dtgen` is an
**interactive** program: for a cross-compiler target it asks on stdin
whether the host's C compiler provides each data type the target needs
("Are you building a cross-compiler? Type y or n [y]:", then one prompt
per datatype). That is fine at a real terminal, but `dtgen` calls
`fgets()` on stdin with no end-of-file check. If stdin is not an
interactive terminal (piped, redirected from `/dev/null`, run under a
script, CI, or backgrounded without a controlling tty), every `fgets()`
call returns immediately on EOF without blocking, the uninitialized
buffer it would have filled almost never happens to read as `'y'` or
`'n'`, and the prompt loop spins indefinitely -- printing megabytes of
"Type y or n" output per second until the disk fills or the process is
killed. This is **not** a 78K/3-specific bug; it is latent in vbcc's own
`dtgen.c` for any cross-compiler target built non-interactively.

These two files are `dtgen`'s actual output for the 78K/3's
`machine.dt`, generated once (interactively, with every prompt answered
by its own computed default -- i.e. an ordinary, correct answer) on a
standard 64-bit Linux host with GCC. Every type in the result maps
directly onto a native GCC/Linux type with no byte-swapping or
conversion shims (`zint`/`zpointer` -> `unsigned short`, `zllong` ->
`signed long`, IEEE `float`/`double` straight through, etc.), which is
exactly the host environment this repository's `README.md` documents
("Build (Linux / GCC)"). `build.sh` copies these two files into the
extracted vbcc tree's object directory, with a timestamp newer than
both `machine.dt` and `dtgen`, before running `make` -- so `make`'s own
dependency check sees `dt.h`/`dt.c` as already up to date and never
invokes `dtgen` at all. No upstream file is patched to make this work.

## Re-generating for a different host

The committed files are pinned for 64-bit Linux/GCC, the supported build
environment. On a host with different C type widths or endianness, you must
regenerate them interactively before using them. Do **not** simply delete
these checked-in files: `build.sh` always copies them into the extracted
source tree, so deleting them makes the build fail.

After a successful normal build has created `build/src/vbcc/`, run from
the repository root with a real terminal attached:

```sh
cd build/src/vbcc
./bin/dtgen machines/78k3/machine.dt objects/78k3/dt.h objects/78k3/dt.c
cd ../../..
cp build/src/vbcc/objects/78k3/dt.h vbcc/machines/78k3/generated/dt.h
cp build/src/vbcc/objects/78k3/dt.c vbcc/machines/78k3/generated/dt.c
make
```

Answer the interactive prompts for the actual host C compiler. This
replaces the pinned files, then rebuilds the toolchain using the new
generated data. These host-specific changes should be reviewed separately
before being shared; no other host configuration has been validated here.
