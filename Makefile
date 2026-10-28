# NEC 78K/3 toolchain: convenience targets for the existing build script.
# The original vbcc, vasm and vlink source trees are not modified in place.

.DEFAULT_GOAL := all

# Default to one parallel Make job per available logical CPU.
# Explicit make -jN / --jobs=N flags take precedence.
# Match the TK-x4x Toolkit's GNU Make convention.
ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 1)
endif

.PHONY: all build clean rebuild test check verify help

# Keep GNU Make's jobserver accessible to the nested builds in build.sh.
all build:
	+./build.sh

clean:
	./build.sh --clean

# Force a clean build in this order, even when make is run with -j.
rebuild:
	$(MAKE) clean
	$(MAKE) all

verify:
	sha256sum -c upstream/SHA256SUMS

# Build and link smoke tests; the output is not bootable radio firmware.
test check: all
	./bin/vasm78k3_std -Fbin -o build/nop.bin tests/nop.asm
	@printf '\000' | cmp - build/nop.bin
	./bin/vbcc78k3 -o=build/smoke.asm tests/smoke.c
	./bin/vasm78k3_std -Fvobj -o build/smoke.o build/smoke.asm
	./bin/vasm78k3_std -Fvobj -o build/link_start.o tests/link_start.asm
	./bin/vlink -T tests/link.ld -b rawbin1 -o build/link.bin build/link_start.o build/smoke.o
	@test -s build/link.bin

help:
	@printf '%s\n' \
	  'NEC 78K/3 toolchain build targets:' \
	  '  make          Build compiler, assembler, linker in parallel (default)' \
	  '  make -j4     Limit parallel compilation to four jobs' \
	  '  make clean    Remove build/ and bin/ only' \
	  '  make rebuild Clean, then build everything again' \
	  '  make verify  Verify the original upstream source archives' \
	  '  make test    Build; test opcode, C compilation, assembly and linking' \
	  '  make help    Show this message'
