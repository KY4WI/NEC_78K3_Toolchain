#!/usr/bin/env bash
# Assemble and build the NEC 78K/3 toolchain from original upstream archives.
# Originals are never modified; all operations occur inside build/.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UPSTREAM="$ROOT/upstream"
BUILD="$ROOT/build"
BIN="$ROOT/bin"

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
say() { printf '\n==> %s\n' "$*"; }
usage() {
  cat <<'HELP'
Usage: ./build.sh [--help|--clean]
  (no arguments)  Build vbcc78k3, vasm78k3_std, and vlink.
                  Inherits GNU Make -j settings; direct invocation
                  defaults to the detected CPU count.
  --clean         Remove generated build/ and bin/ directories.
  --help          Show this help.

The original, unmodified source archives are included:
  upstream/vbcc-10_JAN_2026.tar.gz
  upstream/vasm-v2.0f.tar.gz
  upstream/vlink-v0.18a.tar.gz
  upstream/SHA256SUMS
Read upstream/README.md for details.
HELP
}
if [[ $# -gt 1 ]]; then usage; exit 2; fi
case "${1:-}" in
  --help|-h) usage; exit 0 ;;
  --clean) rm -rf -- "$BUILD" "$BIN"; echo "Removed build/ and bin/"; exit 0 ;;
  "") ;;
  *) usage; exit 2 ;;
esac

for cmd in gcc make tar patch install cp; do
  command -v "$cmd" >/dev/null 2>&1 || die "Required tool not found: $cmd"
done

# Respect an explicit GNU Make -j setting and its jobserver when invoked by
# the top-level Makefile. Direct invocation uses the CPU count unless
# MAKEFLAGS already specifies a job count (or intentionally unlimited -j).
if [[ ! " ${MAKEFLAGS:-} " =~ [[:space:]](-j([0-9]+)?|--jobs(=[0-9]+)?)[[:space:]] ]]; then
  cpu_jobs="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || printf '1\n')"
  [[ "$cpu_jobs" =~ ^[1-9][0-9]*$ ]] || die "Invalid CPU job count: $cpu_jobs"
  export MAKEFLAGS="${MAKEFLAGS:+$MAKEFLAGS }-j$cpu_jobs"
fi
[[ -d "$UPSTREAM" ]] || die "Missing upstream/ directory."

vbcc_archive="$UPSTREAM/vbcc-10_JAN_2026.tar.gz"
vasm_archive="$UPSTREAM/vasm-v2.0f.tar.gz"
vlink_archive="$UPSTREAM/vlink-v0.18a.tar.gz"
for archive in "$vbcc_archive" "$vasm_archive" "$vlink_archive"; do
  [[ -f "$archive" ]] || die "Missing original archive: $archive"
done
[[ -f "$UPSTREAM/SHA256SUMS" ]] || die "Missing upstream/SHA256SUMS"
command -v sha256sum >/dev/null 2>&1 || die "sha256sum is required to verify original archives."
say "Verifying original upstream archive checksums"
(cd "$ROOT" && sha256sum -c upstream/SHA256SUMS) || die "Archive checksum verification failed."

# Only generated directories are removed. Never touch upstream/ or backends.
rm -rf -- "$BUILD" "$BIN"
mkdir -p "$BUILD/src" "$BIN"
extract() {
  local name="$1" archive="$2"
  local dest="$BUILD/src/$name"
  printf 'Extracting %s\n' "$(basename "$archive")" >&2
  # Each original archive already has a top-level vbcc/, vasm/ or vlink/.
  # Extract once into build/src/ to avoid build/src/vbcc/vbcc, etc.
  tar -xf "$archive" -C "$BUILD/src" || die "Could not extract $archive"
  [[ -f "$dest/Makefile" ]] ||
    die "Unexpected directory layout in $(basename "$archive"): expected $name/Makefile"
  printf '%s\n' "$dest"
}
vbcc_src="$(extract vbcc "$vbcc_archive")"
vasm_src="$(extract vasm "$vasm_archive")"
vlink_src="$(extract vlink "$vlink_archive")"

[[ -d "$vbcc_src/machines" ]] || die "Invalid vbcc archive: machines/ missing."
[[ -d "$vasm_src/cpus" && -f "$vasm_src/syntax/std/syntax.c" ]] || die "Invalid vasm archive."
[[ ! -e "$vbcc_src/machines/78k3" ]] || die "Upstream vbcc already contains a 78k3 backend. Inspect it before overwriting."
[[ ! -e "$vasm_src/cpus/78k3" ]] || die "Upstream vasm already contains a 78k3 backend. Inspect it before overwriting."

say "Installing separately maintained NEC 78K/3 backends"
cp -R "$ROOT/vbcc/machines/78k3" "$vbcc_src/machines/"
cp -R "$ROOT/vasm/cpus/78k3" "$vasm_src/cpus/"
cp "$ROOT/vasm/Makefile.78k3" "$vasm_src/Makefile.78k3"

# vbcc's own build generates objects/78k3/dt.{h,c} by running bin/dtgen,
# which is INTERACTIVE for any cross-compiler target (it asks on stdin
# whether the host compiler provides each data type the target needs).
# dtgen's prompt loop never checks for end-of-file on stdin, so without a
# real terminal attached (piped, redirected, scripted, CI) it spins
# printing prompts indefinitely instead of failing or blocking -- this is
# latent in vbcc's own dtgen.c, not something this project's backends
# trigger. Seed the already-generated, pinned-for-Linux/GCC dt.h/dt.c
# (see vbcc/machines/78k3/generated/README.md) with a timestamp newer
# than both machine.dt and bin/dtgen, so make's own dependency check
# finds them already up to date and never calls dtgen. bin/dtgen must be
# built here (with the exact recipe make.rules uses) *before* the seed
# files are placed/touched: `make` would otherwise compile it fresh
# during the same run and leave it newer than the seeded dt.h/dt.c,
# triggering regeneration anyway. No upstream file is patched to do this.
say "Pre-building dtgen and seeding its (interactive) output"
mkdir -p "$vbcc_src/bin" "$vbcc_src/objects/78k3"
(cd "$vbcc_src" && gcc -std=c9x -g -Wno-format-security -DHAVE_AOS4 \
  datatypes/dtgen.c -o bin/dtgen -Idatatypes -lm) ||
  die "Could not pre-build dtgen."
cp "$ROOT/vbcc/machines/78k3/generated/dt.h" "$vbcc_src/objects/78k3/dt.h"
cp "$ROOT/vbcc/machines/78k3/generated/dt.c" "$vbcc_src/objects/78k3/dt.c"
touch "$vbcc_src/objects/78k3/dt.h" "$vbcc_src/objects/78k3/dt.c"

say "Applying documented vasm standard-syntax parser patch"
(cd "$vasm_src" && patch --batch --forward --fuzz=0 -p1 < "$ROOT/patches/vasm-std-78k3.patch") ||
  die "The vasm patch did not apply. Check the original vasm version; do not force it."

say "Building vbcc for the 78K/3"
(cd "$vbcc_src" && mkdir -p bin objects && make TARGET=78k3 all)
say "Building vasm for the 78K/3"
(cd "$vasm_src" && mkdir -p obj && make -f Makefile.78k3 CPU=78k3 SYNTAX=std)
say "Building unmodified vlink"
(cd "$vlink_src" && mkdir -p objects && make)

for spec in "$vbcc_src/bin/vbcc78k3:vbcc78k3" "$vbcc_src/bin/vc:vc" \
            "$vasm_src/vasm78k3_std:vasm78k3_std" "$vlink_src/vlink:vlink"; do
  src="${spec%:*}"; target="${spec##*:}"
  [[ -f "$src" ]] || die "Build did not produce $src"
  install -m 755 "$src" "$BIN/$target"
done
say "Toolchain binaries available in bin/"
printf '  vbcc78k3, vc, vasm78k3_std, vlink\n'
printf 'Development software: inspect generated code before using it on hardware.\n'
