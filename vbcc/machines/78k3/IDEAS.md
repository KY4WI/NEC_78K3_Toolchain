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
(`FE80h`-`FEFFh`), switched with the 2-byte `SEL RBn` instruction. The
stock Kenwood mask ROM never executes a single `SEL` (confirmed by
grepping its full disassembly), so on this specific target, banks 1-7
are entirely free.

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
