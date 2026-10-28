/* =====================================================================
 * TK-840 replacement-firmware: CALLT (78K/III fast-call) support.
 *
 * The 78312A's CALLT instruction is a 1-byte call through a fixed
 * 32-entry, 2-byte-per-entry address table at 0x0040-0x007F (CALLT's
 * operand is a hardware 5-bit field, so exactly 32 slots -- not
 * 0x0040-0x007E as an earlier revision of the hardware doc had it, off
 * by one byte). The stock TK-x4x firmware uses this heavily: panel-byte
 * send, EEPROM helpers, the BU4094/M62363 writes, UART send/receive --
 * see Kenwood_TK-x4x_Reverse_Engineering docs/TK-x4x_Hardware_Communication.md section 17.1 for the full
 * evidence trail.
 *
 * vbcc78k3's __callt_slotN attribute (machines/78k3/machine.c) makes a
 * function's call sites compile straight to "callt [N]" instead of a
 * 3-byte absolute CALL. It covers two distinct real-world cases, told
 * apart automatically by whether the attributed function has a body in
 * this translation unit:
 *
 *   - A function DEFINED here with __callt_slotN: the compiler both
 *     compiles it normally AND places its own address into the table
 *     at slot N (see machine.c's cleanup_cg()). Use this for a new C
 *     function you want reachable via a fixed, known slot.
 *
 *   - A function only DECLARED with __callt_slotN (a prototype, no
 *     body here): every call site still compiles to "callt [N]", but
 *     nothing gets written into the table for it -- exactly right for
 *     calling an existing resident/mask-ROM routine (or a hand-written
 *     ABI shim to one, e.g. resident_api.h's pattern) through a slot
 *     that's already populated some other way (Kenwood's own reset
 *     code, or your own hand-assembled table entry). This mirrors the
 *     stock firmware's own use of entries like "CALLT -> 808F -> BR
 *     1F24" -- a shim, not the final implementation, behind the slot.
 *
 * A third attribute, bare __callt (CALLT_AUTO below, no slot number),
 * covers a third case: a new C function you want reachable via CALLT,
 * but don't care which of the 32 slots it lands in -- the compiler picks
 * any slot not explicitly claimed elsewhere in the file. Only meaningful
 * on a function you're actually defining (there's no address to assign a
 * slot to otherwise) -- it does NOT work for the declare-only/resident-
 * routine case above, since that case's whole point is a slot number
 * fixed by something outside the compiler's control, not a free choice.
 * 2026-10-11: deliberately NOT extended to auto-fill slots nobody asked
 * for by picking the busiest-called functions automatically -- see
 * machine.c's g_attr_name[] comment for why that's deferred rather than
 * built now. Hand-pick known-hot functions with __callt_slotN instead.
 *
 * vbcc's HAVE_TARGET_ATTRIBUTES mechanism (declaration.c, shared core)
 * recognizes these as bare keyword tokens straight in the declaration-
 * specifier position -- the same place __interrupt goes, written before
 * the function name, e.g. "void __interrupt foo(void)" -- not as a
 * GCC-style __attribute__((...)) wrapper, and with no support for a
 * parenthesized argument like "callt_slot(19)" either way. So the
 * compiler-recognized spelling is one bare name per slot (__callt_slot0
 * .. __callt_slot31), plus the one further bare name "__callt" for the
 * auto case. CALLT_SLOT(n)/CALLT_AUTO below get you ergonomic syntax
 * anyway -- CALLT_SLOT(n) via preprocessor token-pasting (##), gluing
 * the slot number onto the keyword before the compiler's parser ever
 * sees it, and CALLT_AUTO as a plain alias for symmetry -- so none of
 * this needed any change to vbcc's shared core parsing code.
 *
 * Usage (same position as __interrupt -- before the function name):
 *     void CALLT_SLOT(19) my_isr_helper(void);  // definition: placed at slot 19
 *     void CALLT_SLOT(3) eeprom_seq_read(u8 addr, void *dest, u8 count);  // declaration only: existing slot 3 routine
 *     void CALLT_AUTO small_helper(void);        // definition: compiler picks any free slot
 *
 * Whichever attribute you use, repeat the SAME one on every declaration
 * of that function, including a forward prototype -- confirmed the hard
 * way: a forward declaration with no CALLT attribute at all, followed by
 * a definition that adds one, compiles without error (just a generic
 * "redeclaration ... differing attribute" warning, easy to miss) but any
 * call site that saw the bare, attribute-less declaration first silently
 * falls back to a plain 3-byte CALL instead of CALLT -- it resolves the
 * attribute from whichever declaration was in scope at that call site,
 * not from the function's eventual definition.
 *
 * A linker script mapping the "calltbl" section to the real table
 * address (0x0040) is required at link time -- this header and the
 * compiler attribute only get you the "callt [N]" instructions and the
 * table CONTENTS; placing that section at 0x40 in memory is a separate,
 * ordinary linker-script step:
 *     SECTIONS { calltbl 0x40 : { *(calltbl) } .text 0x100 : { *(.text) } }
 * (addresses above are illustrative -- .text's real start depends on
 * the rest of this project's eventual memory map, e.g. avoiding the
 * mask ROM / vector area per Kenwood_TK-x4x_Reverse_Engineering docs/TK-x4x_Hardware_Communication.md section 2.3).
 * ===================================================================== */

#ifndef CALLT_H
#define CALLT_H

#define CALLT_SLOT(n) __callt_slot##n
#define CALLT_AUTO __callt

#endif
