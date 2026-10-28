#!/bin/sh
# Regenerate mnemonics_gen.c from shared/Opcodes.h.
#
# Run this from cpus/78k3/ (or anywhere -- it cd's there itself) after any
# change to shared/Opcodes.h, shared/Operand.h or shared/Mnemonic.h. It
# compiles the small standalone host generator gen_mnemonics.c and runs it,
# capturing its stdout as mnemonics_gen.c (its stderr is a one-line
# included/excluded row-count summary, printed here too).
#
# NOTE: gen_mnemonics.c deliberately does NOT emit "#include \"vasm.h\"" or
# "#include \"shared/Operand.h\"" into mnemonics_gen.c -- that file is
# #include'd directly from cpu.c, after cpu.c has already included both,
# and vasm.h has no include guard (see cpu.c's own comment above its
# "#include \"mnemonics_gen.c\"" line).
set -e
cd "$(dirname "$0")"
gcc -std=c99 -I. -I.. -I../.. -o /tmp/gen_mnemonics_78k3 gen_mnemonics.c
/tmp/gen_mnemonics_78k3 > mnemonics_gen.c

# mnemonics_gen.c is #include'd from cpu.c, not compiled as its own
# translation unit, so make(1) has no dependency rule linking it to
# cpu.c's object file -- touch cpu.c so the next `make` notices it needs
# a rebuild even though cpu.c's own mtime/content didn't change.
touch cpu.c
