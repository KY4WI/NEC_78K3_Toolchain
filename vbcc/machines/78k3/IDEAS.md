# vbcc78k3 ideas

Features considered for `machines/78k3/machine.c` (the 78K/III code
generator for vbcc) but not built -- either not yet justified by a real
need, or needing more groundwork first. Kept here, in the same place as
[`BUGS.md`](BUGS.md), so a later look doesn't have to re-derive the
reasoning from scratch. Nothing here is implemented; "status" on each
entry says what would have to happen before it is.

---

## Idea #1: a function attribute that puts an interrupt handler in its own register bank, instead of push/pop

**Status: brainstormed only (2026-10-29), not designed, not started.**

### The idea

The NEC 78K/III has 8 general-register banks (`RB0`-`RB7`), not just the
one most code ever sees -- 16 bytes each, mapped to a fixed RAM window
(`FE80h`-`FEFFh`, bank *n* at `FEF0h` - 16*n), switched with the 2-byte
`SEL RBn` instruction. The stock Kenwood firmware never executes a single
`SEL` (confirmed by grepping the full disassembly).

**Correction (2026-10-29):** an earlier version of this note said that
made banks 1-7 "entirely free". That was too strong. A bank is 16 bytes of
ordinary RAM, and the stock app uses nearly all of `FE80h`-`FEECh` as
variables; the mask ROM uses `FEBFh`, `FEC0h`-`FECFh` and `FED3h`-`FEDFh`
and never names anything in `FE80h`-`FEAFh` (banks 7, 6, 5). So this only
works in an image that replaces the stock app, using banks the mask ROM
routines it calls don't touch. Details and manual references: section 19.4
of `docs/hardware/Resident_ROM_and_Interrupts.md` in the Reverse_Engineering
repo.

That makes `SEL RBn` a candidate replacement for the usual
`PUSH {ax}` / `POP {ax}` an interrupt service routine uses to protect a
register it needs as scratch: switch to an unused bank at ISR entry (that
bank's own `AX` is fair game, with no stack traffic and without touching
whatever the interrupted code had in bank 0's `AX`), do the work, switch
back to bank 0 before `reti`. Both forms cost 2 bytes each way, so this
isn't about code size or speed over push/pop -- it's about freeing a
register the ISR needs without the stack traffic, and about an
instruction-level option vbcc's current codegen has no notion of at all.

This is proven as hand-written assembly: see
[`tools/probes/bankisr/`](https://github.com/KY4WI/Kenwood_TK-x4x_Reverse_Engineering/tree/main/tools/probes/bankisr)
in the Reverse_Engineering repo, which repoints the real DDS/TMF0
interrupt at exactly this pattern and (pending its own bench run) checks
whether it's safe across a real, firing interrupt. The question here is
whether/how a C-level version of the same trick could be exposed through
this compiler, not whether the instruction sequence itself works.

### Precedent

SDCC does the equivalent thing for the 8051 with `__using(n)`: a function
attribute marking an interrupt handler as running in register bank `n`,
telling the compiler to skip the normal save/restore and switch banks
instead. The 8051 and 78K/III situations are close enough architecturally
(both have more than one hardware register bank, both only make switching
them worthwhile for isolated, self-contained handler-shaped functions)
that `__using`'s design is a reasonable model to start from rather than
inventing one from nothing.

### What it would take in this backend

- A new attribute or pragma (`__attribute__((bank(n)))`, or something
  pragma-based instead, following whatever vbcc's existing attribute
  plumbing already supports) marking a function as entirely resident in
  one register bank.
- Codegen changes in `machine.c`'s interrupt-function prologue/epilogue
  path: emit `SEL RBn` on entry and `SEL RB0` before `reti` instead of
  the current save/restore sequence, and allocate that function's own
  locals/temporaries into the bank's registers rather than the normal
  stack-frame scheme this backend already uses for everything else.
- The harder part: vbcc's register allocator has no concept of "which
  bank" at all right now. Any *other* function must never be allowed to
  assume bank-0 register contents survive a call into a banked function
  (and the banked function, symmetrically, can't assume anything about
  bank-0's contents belongs to it). That's a real correctness constraint
  on the allocator, not just a code-generation detail, and it's the part
  that hasn't been thought through yet.

### Why this only fits interrupt handlers, not general C

A banked function has no sane way to receive C-level arguments or return
a C-level value through registers the normal calling convention expects
to find in bank 0 -- it would need to communicate through memory only,
exactly like an ISR already does. So this is an ISR-prologue
optimization with a C-level attribute on it, not a general-purpose
calling convention; it would never make sense on an ordinary function.

### Whether it's worth building

Marginal as things stand: it saves no bytes over push/pop (2 bytes
either way) and the stock firmware's own interrupt handlers get by fine
on plain push/pop with no banking at all, so the real win is narrow --
freeing up a register across the interrupt boundary for something
push/pop can't do as cleanly, not speed or size. Worth revisiting once
`bankisr` has an actual bench result (if register banking turns out
*not* to be safe across a live interrupt on real silicon, this whole
idea is moot regardless of what the compiler could do), and only if a
real C image on this project turns out to need it.

---

## Idea #2: use the hardware context switch (`BRKCS`/`RETCS`) as a "subroutine in its own bank"

**Status: brainstormed only (2026-10-29), not designed, not started.**
Follows from Idea #1 and a question about preloading a bank's registers by
writing their RAM addresses and then switching banks.

### The idea

The chip already has exactly that, in hardware (uPD78312A manual 5.3, pp.
5-25 to 5-28; summarized in the Reverse_Engineering repo's
`Resident_ROM_and_Interrupts.md` 19.4). Each bank's `R5:R4` is a
pre-stored entry address. `BRKCS RBn` (2 bytes) selects the bank, jumps to
that entry address and saves the old `PC` in `R5:R4` and the old `PSW` in
`R7:R6`, with nothing pushed on the stack. `RETCS !entry` (3 bytes) returns
and writes `entry` back into `R5:R4` so the bank is ready for the next
call. A maskable interrupt whose `xxIC` bit 4 is set does the same thing
by itself (bank chosen by the interrupt's priority level, 11 states to the
first instruction). The bank's other registers keep their values between
calls, so a routine can hold its state (a DDS phase accumulator, say) in
registers permanently, and a caller can pass arguments by writing the
bank's RAM addresses first.

### What it would take in this backend

- A function attribute (for example `__attribute__((bank(n)))`) meaning
  "entered by `BRKCS RBn` or by a context-switched interrupt, ends with
  `RETCS`", with the backend emitting `RETCS !self` and, for the `BRKCS`
  case, `SET1 CCW.0` before it (the manual requires it).
- Call sites for such a function emit `BRKCS RBn` instead of `CALL`, with
  arguments written to the bank's register addresses and results read back
  from them: a different calling convention from the stack-based one this
  backend uses today.
- Start-up code (or the attribute's support file) that stores the entry
  address into the bank's `R5:R4`. Static initialization of a RAM window
  that overlaps ordinary variables needs the linker script to reserve it.
- Same allocator constraint as Idea #1: no other function may assume
  anything about registers across a banked call.

### Limits

One saved `PC` per bank, so no recursion or re-entry. Needs the bank's 16
bytes reserved (see the Idea #1 correction). `RETCS` must be the only way
out. Nothing here has been tried on hardware, and the cycle counts of
`BRKCS`/`RETCS` have not been looked up, so it is not known whether it
beats `CALL`/`RET` or `PUSH`/`POP`.

### Whether it's worth building

Only if a real image needs register-resident state across an interrupt,
such as a tight DDS or modem tick. First step would be a hand-written
assembly probe in the Reverse_Engineering repo (a `bankisr` follow-up that
uses a context-switched TMF0), not compiler work.
