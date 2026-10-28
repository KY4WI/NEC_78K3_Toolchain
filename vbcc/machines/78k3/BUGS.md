# vbcc78k3 compiler bugs

Confirmed defects in `machines/78k3/machine.c` (the 78K/III code
generator for vbcc), found while bringing up firmware for the Kenwood
TK-x4x radios (NEC µPD78312A) on real hardware. The images named below
(`phase1`, `selftest`, `panelprobe`) are in the
[Kenwood_TK-x4x_Reverse_Engineering](https://github.com/KY4WI/Kenwood_TK-x4x_Reverse_Engineering)
repository, `tools/probes/`; regression tests are in
[NEC_78KIII_Emulator](https://github.com/KY4WI/NEC_78KIII_Emulator),
`tests/`. Each entry is proven by direct inspection of the
generated assembly, not inferred from symptoms alone -- the same evidence
bar as every other finding in this project. Until a bug is actually fixed
here, new C code in any image (`phase1`, `selftest`, `panelprobe`, or
whatever comes after) must route around it using the patterns described
below.

---

## Bug #1: unsigned relational comparisons can silently compile to a SIGNED branch instruction

**Status: FIXED in the compiler itself (2026-10-08). The panelprobe BETA_33
`b3_subcode_valid()` switch workaround is no longer required for new code,
but is being left in place in that one function as defense-in-depth rather
than reverted -- see "The real fix" below for what changed and how it was
verified.**

**Severity: affects any `<`, `<=`, `>`, `>=` comparison on an `unsigned
char`/`unsigned short` (this project's `u8`/`u16`) value, wherever that
value could plausibly cross the "top bit" boundary (`0x80` for a `u8`,
`0x8000` for a `u16`) against a smaller constant. Silently produces the
WRONG branch direction for any value at or above that boundary -- no
warning, no crash at compile time, just a function that misbehaves only
for a subset of its possible inputs.**

### How it was found

panelprobe BETA_32 added a guard to `panel_test_b3_subcode()` (`panel.c`):

```c
if (sub < PANEL_B3_SUBCODE_MIN || sub > PANEL_B3_SUBCODE_MAX) {
    /* report "SKIP", don't send */
    return;
}
```

`sub` is `u8`, `PANEL_B3_SUBCODE_MIN`/`MAX` are `0x01`/`0x0D` (1/13). Real
bench testing found this guard did NOT stop candidates like `0x8E`, `0x9E`,
`0xCD`, `0xCE` from being sent -- every one of them locked up the real
panel, exactly what the guard existed to prevent, and the "SKIP" message
never printed for any of them.

### Root cause

The generated code for `sub > 13` was:

```asm
mov   a,[sp+4]
cmp   a,#13
ble   l88        ; BUG: signed branch, for an unsigned comparison
```

`ble` is the NEC 78K/III's **signed** "less than or equal" branch (condition:
`(OV xor S) or Z` -- NEC datasheet sec 6.20). The unsigned equivalent is
`bnh` ("branch not higher", condition: `(Z or CY) = 1`). `cond_branch_mnemonic()`
in `machine.c` has both:

```c
case BLE: return uns?"bnh":"ble";
```

...so the mechanism to pick the right one already exists. The bug is in
what gets passed as `type` to decide `uns`:

```c
case BEQ: case BNE: case BLT: case BGE: case BLE: case BGT:
    emit(f,"\t%s\t%s%d\n",cond_branch_mnemonic(c,p->typf),labprefix,p->typf);
```

For a conditional-branch IC, vbcc's generic IR (`supp.h`) overloads the
`typf` field: `#define iclabel(p) (p->typf)` -- for `BEQ`/`BNE`/`BLT`/`BGE`/
`BLE`/`BGT`/`BRA`/`LABEL` ICs, `typf` holds the **branch target's label
number**, not type information (confirmed from the `LABEL` case two lines
above, which prints `p->typf` directly as the numeric label: `l88`, `l106`,
etc.). `cond_branch_mnemonic(c, p->typf)` is therefore evaluating
`label_number & UNSIGNED` (`UNSIGNED` = `256`, i.e. bit 8) as if it were a
real type bitmask. The actual unsigned-ness of the COMPARE IC that set the
flags (computed correctly, in the local `t`/`uns` at the top of `gen_code`'s
loop for that one IC) is never carried forward to the following branch IC.

For the specific failing case: the branch's target label happened to be
`l88` (88 decimal, binary `01011000`) -- bit 8 (value 256) is not set, so
`uns` evaluated false, and the signed `ble` was emitted regardless of the
operand's real (unsigned) type. This isn't a hardware fluke or a flaky
bench unit -- it is fully deterministic given that label number, and would
flip to the (coincidentally correct, for different reasons) `bnh` for a
label number >= 256 purely by chance, not by design.

**Why this went unnoticed for 32 prior betas:** `BEQ`/`BNE` are
signedness-independent (confirmed correct, `Z` flag alone) and unaffected.
Every prior `BLT`/`BGE`/`BLE`/`BGT` comparison in this project's compiled
code so far happened to only ever compare values that stay below `0x80`
(u8) or `0x8000` (u16) -- loop counters like `retry < 5`, `i < 9`, array
indices like `idx >= 0x1F`, etc. For values confined to that range, the
signed and unsigned interpretations of the bit pattern are IDENTICAL, so
the wrong branch instruction still produces the right answer by luck. The
very first time this project wrote an unsigned relational comparison
against a byte that is allowed to range over the FULL `0x00`-`0xFF` --
`picker_candidate`, directly settable by the user via Grp Up/Dn -- the bug
finally had a chance to matter, and it did, immediately.

### Confirmed scope

Reproduced in isolation with a minimal two-function test file
(`u8 range_check(u8 sub) { if (sub < 1 || sub > 13) return 0; return 1; }`)
-- compiles to the identical buggy `cmp`/`ble` pattern. This is a general
defect in the backend, not specific to `panel_test_b3_subcode()` or to
`panelprobe`. It can affect `phase1` and `selftest` too, for any unsigned
relational comparison they contain that crosses the boundary -- not
audited yet (see TODO below).

### Confirmed-safe workaround (use until this is fixed in the compiler)

`BEQ`/`BNE` are unaffected (signedness never enters their condition). A
`switch` statement over a small set of literal integer values was
confirmed (same minimal test file) to compile to a chain of plain
`cmp`/`be`/`bne` instructions -- no `ble`/`bgt`/`blt`/`bge` anywhere, for
either the compiler's switch-implementation strategy or the final
assembly. **Until this bug is fixed, replace any unsigned relational range
check whose operand can plausibly reach `0x80`-`0xFF` (u8) or
`0x8000`-`0xFFFF` (u16) with an explicit `switch`/`case` (or an equivalent
chain of `==` tests) instead of `<`/`<=`/`>`/`>=`.** See
`panelprobe/panel.c`'s `b3_subcode_valid()` (BETA_33) for the pattern.

This is NOT a blanket rule for every comparison in the codebase -- a
counter that is contractually bounded well under `0x80` (e.g. `for (i = 0;
i < 9; i++)`) is unaffected and does not need rewriting. The rule is
specifically: unsigned, relational (not equality), AND the operand's real
range can include a value with the top bit set.

### The real fix (done 2026-10-08)

`machine.c` now threads the COMPARE/TEST IC's real operand signedness
through to the branch IC that reads the flags it set, instead of reusing
`p->typf` on the branch IC (which, as explained above, is that branch's
own jump-target label number -- never type information):

1. A new file-scope flag, declared right after `cond_branch_mnemonic()`:
   ```c
   static int last_compare_was_unsigned=0;
   ```
2. `case COMPARE:` sets it from the COMPARE IC's own (correctly computed)
   type, as its very first action, before anything else runs for that IC:
   ```c
   last_compare_was_unsigned=(t&UNSIGNED)!=0;
   ```
3. `case TEST:` sets it the same way, defensively -- `BEQ`/`BNE` are the
   only branches that can follow a `TEST` in this backend's actual output
   (signedness-independent, so this isn't strictly load-bearing today),
   but it costs nothing and closes off one way this bug could be
   silently reintroduced if that ever changes.
4. The `case BEQ: case BNE: case BLT: case BGE: case BLE: case BGT:` branch-
   emission site now reads that flag instead of `p->typf`:
   ```c
   emit(f,"\t%s\t%s%d\n",
        cond_branch_mnemonic(c,last_compare_was_unsigned?UNSIGNED:0),
        labprefix,p->typf);
   ```
   (`p->typf` is still used for the label number itself, which is the
   one thing it actually holds -- only the signedness decision stopped
   using it.)

This is safe because every `BLT`/`BGE`/`BLE`/`BGT`/`BEQ`/`BNE` IC this
backend actually emits immediately follows the `COMPARE`/`TEST` IC that
set the flags it reads, with no intervening IC except bookkeeping-only
`NOP`/`FREEREG`/`ALLOCREG` -- confirmed by scanning this backend's real
generated IC sequences, not assumed.

### How the fix was verified

- **Isolated reproduction**: the exact failing pattern from BETA_32
  (`if (sub < 1 || sub > 13) return 0;`, as a standalone `u8
  range_check(u8 sub)` function) now compiles to the correct unsigned
  branch:
  ```asm
  _range_check:
  	mov	a,[sp+2]
  	cmp	a,#0
  	be	l20
  	mov	a,[sp+2]
  	cmp	a,#13
  	bnh	l21          <- was "ble l21" (wrong/signed) before the fix
  l20:
  	mov	a,#0
  	br	l18
  l21:
  	mov	a,#1
  l18:
  ```
- **Emulator regression**: `tests/run_tests.sh` -- still 7/7 passing
  with the fixed compiler.
- **Full rebuild of all three real firmware images** (`phase1`,
  `selftest`, `panelprobe`) with the fixed compiler -- all three still
  build to the exact required size (26112 bytes), with no warnings or
  errors beyond the pre-existing, harmless ones (`u8` redeclaration,
  `main`'s return type) already present before this fix.
- **Stack-frame safety re-checked**: diffed every function's entry
  `subw ax,#N` against a build from the unfixed compiler -- identical in
  every function, in all three images. This fix only changes which
  branch mnemonic gets printed; it does not touch register allocation or
  stack-slot assignment, so the existing 30-byte-budget analysis for
  every function in every image still holds unchanged.
- **An unexpected (good) side effect, found while diffing panelprobe's
  generated assembly line-by-line against the unfixed compiler's
  output**: five more comparisons in already-shipped, already-tested code
  (`panel.c`'s `_panel_display_text`, `_panel_handshake` (two spots),
  `_panel_test_picker_send`, `_panel_test_secondary_frame`, and one in
  `main.c`) had ALSO been silently compiling to the wrong signed
  branch (`blt`/`bge` instead of `bl`/`bnl`) all along -- exactly the bug
  described above, just never caught because every one of those specific
  comparisons happens to be against a small constant (`9`, `2000`, `5`,
  `10`, `31`) that every real operand value stays safely under, so the
  wrong instruction still produced the right answer by the same luck
  described in "why this went unnoticed" above. **None of these were
  behavior bugs in practice** -- nothing about `panelprobe`'s observed
  behavior changes. They're listed here because it's a second, independent
  confirmation that this bug was real and present well beyond the one
  call site that happened to get caught on the bench, and because the
  fix makes all of them provably correct for their full possible input
  range now, not just the range that happened to get exercised.
  Interestingly, the unsigned branch forms (`bl`/`bnl`/`bnh`/`bh`,
  single-flag hardware conditions) are also one byte shorter in machine
  code than their signed counterparts (`blt`/`bge`/`ble`/`bgt`, which need
  an extra byte on this CPU to combine the overflow and sign flags) --
  confirmed directly with a two-line assembler test (`blt` -> 3 bytes,
  `bl` -> 2 bytes, same operands). That's also the full explanation for
  why `panelprobe.bin` differs from the previously-shipped BETA_33 image
  at far more byte offsets than "six fixed branches" would suggest by
  itself: six branches getting one byte shorter shifts the absolute
  address of everything after them, which changes many of the absolute
  call-target bytes elsewhere in the file. The file is still exactly
  26112 bytes (the image is a fixed-size flash region padded out to that
  size, not packed to its content size), and nothing about its behavior
  changed from this.

### Follow-ups

- **Done:** the regression test `t_unsigned_cmp` (NEC_78KIII_Emulator)
  uses values on both sides of `0x80`/`0x8000` under every unsigned
  relational operator, so this bug can't come back unnoticed.
- **Open, panelprobe only: whether to simplify `panel_test_b3_subcode()`'s
  `b3_subcode_valid()`** (the BETA_33 switch-based workaround) back to a
  plain `sub < PANEL_B3_SUBCODE_MIN || sub > PANEL_B3_SUBCODE_MAX` now
  that the compiler is fixed. Leaning toward leaving the switch version in
  place as defense-in-depth (it's immune to this entire bug class by
  construction, independent of whether the compiler stays fixed), but
  this hasn't been decided or asked about yet.

---

## Bug #2: constants read through the wrong field (fixed 2026-10-15)

The code generator read constant operands (shift counts, AND/OR masks,
the increment-by-one check, the 0/1 tests for booleans) from
`val.vmax` whatever the constant's type was. For a narrower type that
field is not set, so the value came from leftover heap contents, and
the output changed with the build directory. In `selftest` it made the
PLL code's `n >> 4` compile to 0.

**Fixed** in KY4WI/vbcc `4069a17`: constants are read through
`eval_const()`, which uses the field that matches the type.

## Bug #3: the address of a static object read as its contents (fixed 2026-10-16)

This was the cause of the "PQRSTUVW" display that `panelprobe` chased
through BETA_18-33.

**Symptom.** `panel_display_text("PANEL OK")` showed `PQRSTUVW`: the
first character right, then one higher per position. An all-`'5'`
string showed `56789`.

**Root cause.** When the address of a static or external object (a
string literal, a static array, a static struct) is used as a value,
vbcc's optimizer marks the operand with the `VARADR` flag. The 78K/III
code generator never checked that flag, so it loaded the object's first
two bytes as if they were the pointer:

```
	mov	a,l22		; 'P' -- should have been  movw ax,#l22
	mov	x,a
	mov	a,l22+1		; 'A'
```

The routine was handed 4150h ("PA"). Nothing answers at 4150h, and on
the radio's multiplexed bus a read of an unmapped address returns its
own low byte, so the loop read 50h, 51h, 52h ... = "PQRSTUVW". The
all-`'5'` string gave 3535h, hence "56789". Indexing a static array by
a variable, `&tbl[i]`, and static struct members reached by address
were wrong the same way.

**Why it looked like a loop problem.** Text sent from bytes built on
the stack, or as constants (the 9-byte burst test, single bytes),
never takes a static object's address, so those always worked. Every
string literal went through a loop, so the loop took the blame.
`benchtest` (assembly) settled it: both its loop and the mask ROM's
unrolled routine displayed correctly.

**Fixed** in KY4WI/vbcc `2945d2e`: a `VARADR` operand is loaded as
`movw ax,#label`; initialized data (pointer tables) gets the bare
label; and an internal error stops the compiler if one ever reaches
the memory-read path again. Regression tests `t_textloop` and
`t_varadr` in KY4WI/NEC_78KIII_Emulator fail with `4069a17` and pass
with `2945d2e`.

**Images.** `selftest` and phase1 are rebuilt with the fix. phase1's
`.s` files had been generated before bug #2 was fixed, so they also
pick up that fix and earlier code generator changes (smaller stack
frames, for one). In phase1, the bug #3 changes are in `main`, `model`
and `ui`, all at calls that pass a string literal. The panel C code
needs no workaround now.

## Bug #4: compare against a static variable emits an invalid instruction (open)

`if (i <= some_static_u8)` can compile to `cmp a,<label>` with the
variable's absolute address. The 78K/III's `CMP A,...` has no 16-bit
address form (only `saddr`, `FE20h`-`FF1Fh`, which the assembler cannot
assume for a relocatable label), so vasm rejects it ("illegal operand
types"). Loud, not silent: the build fails. Workaround: copy the static
into a local first (`volatile u8 prev = some_static_u8;`). Found
2026-10-16 in the TK-x4x probes' `dbg_report_stack()`. Fix: load the
operand into a register (or use `cmp a,[de]` through a pointer) when it
is a static/extern object.

---

## Notes for writing C for this backend

Not bugs, but things to know, checked by compiling, linking and reading
the generated code (moved here from the custom firmware's old
hardware-communication notes, 2026-09-27):

- **Registers and RAM through pointers.** `#define SFR8(a) (*(volatile
  u8 *)(a))` and `SFR16` compile correctly for word writes, byte
  read-modify-write and bit tests. The backend loads the address into
  `DE` and uses `[de+0]`/`[de+1]`, not the short `sfr`/`saddr` forms
  hand-written code uses: correct, but bigger and slower. Write
  timing-critical code (bit-banged serial, the PLL shift) in assembly.
  Possible improvement: lower a constant-address `DREFOBJ` to `sfr`/
  `saddr` addressing.
- **Calling convention.** The caller builds the arguments on the stack;
  the return value comes back in `AX`. The mask ROM's routines take their
  arguments in registers or fixed RAM instead, so C reaches them through
  small assembly shims (the probes' `common/resident_api.s`) rather than
  by calling the address directly.
- **Calling a fixed address.** A function pointer cast from a constant,
  `((void (*)(void))0x1F0F)()`, compiles to a correct indirect call; fine
  for routines that take no arguments.
- **CALLT.** `__callt_slotN` and `__callt` make call sites use `callt`;
  [`support/callt.h`](support/callt.h) and
  [`support/calltbl.ld`](support/calltbl.ld) are the header and linker
  script fragment for them. Not yet used by an image run on the radio.
  On the TK-x4x the application's CALLT table is at `8040h` (read while
  `CCW` `TPF` is set), so the fragment's `0x40` origins need changing for
  that target.

