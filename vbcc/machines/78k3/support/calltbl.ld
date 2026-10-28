/* calltbl.ld -- NEC 78K/III CALLT table linker-script fragment.
 *
 * Generated once, checked in -- these addresses are a fixed hardware
 * fact (CALLT operand is a 5-bit slot index, table is 0x0040-0x007F,
 * 32 slots x 2 bytes), never anything to regenerate per build.
 *
 * Each slot gets its OWN 2-byte MEMORY region, not just its own named
 * output section. That is what actually makes a cross-file slot
 * collision a hard link error instead of silent corruption: plain
 * ``calltblN 0xNN : { *(calltblN) }'' with no MEMORY region still lets
 * vlink silently concatenate two files' same-named contributions at
 * the shared address (confirmed empirically -- exit 0, no warning,
 * the second file's entry overflows into the next slot). Binding each
 * section to a MEMORY region sized to exactly 2 bytes makes vlink
 * reject that overflow outright (confirmed: "No space to fit section
 * FILE(calltblN) ... into memory region", naming the offending file).
 * A slot no file contributes to resolves to a silent zero AT ITS
 * CORRECT ADDRESS -- but only once something else in the overall link
 * has real content at or below this table's own lowest address (0x40).
 * This matters because of a real, non-obvious rawbin1 quirk, confirmed
 * empirically: a raw/flat binary has no inherent base-address field, so
 * vlink's rawbin1 writer starts the OUTPUT FILE at whatever address the
 * first byte of REAL content lands on, not at the lowest address any
 * MEMORY region merely declares. A gap BETWEEN two pieces of real
 * content is correctly zero-padded (confirmed repeatedly above and in
 * task #39's notes) -- but LEADING dead space before the first real
 * byte is silently dropped, not zero-padded. Concretely: linking just
 * this fragment plus a handful of functions where the lowest-numbered
 * claimed slot is, say, 5 (slots 0-4 entirely unclaimed by anyone)
 * produces a file whose byte 0 is slot 5's content at file offset 0,
 * NOT slot 0's content at the intended file offset 0 with slots 0-4
 * zeroed before it -- silently shifting every subsequent address by
 * however many leading empty slots there were. Confirmed the fix:
 * anything with real content at address 0x40 or below (in the full
 * firmware image, the reset vector at 0x0000 almost certainly
 * qualifies, per Kenwood_TK-x4x_Reverse_Engineering docs/TK-x4x_Hardware_Communication.md section 2.3) makes
 * this a non-issue, since real content already anchors the file's true
 * base before this table's address range is ever reached. Don't test
 * this fragment (or any subset of the real memory map) in isolation
 * without something anchoring its lowest address, and when in doubt,
 * check vlink's own symbol/segment map (-M) rather than assuming file
 * offset 0 means address 0x40 (or whatever a MEMORY region's ORIGIN
 * says) -- confirmed above as NOT a safe assumption.
 *
 * vbcc78k3's __callt_slotN attribute (machines/78k3/machine.c) is
 * what emits the calltblN input sections this fragment places -- see
 * its cleanup_cg() and support/callt.h's CALLT_SLOT(n) macro.
 *
 * Include this fragment inside your own project's SECTIONS{}/MEMORY{}
 * script alongside .text/.data/.bss and whatever else the real memory
 * map needs (vector area, mask ROM avoidance -- see
 * Kenwood_TK-x4x_Reverse_Engineering docs/TK-x4x_Hardware_Communication.md section 2.3), not linked standalone.
 * Confirmed empirically: vlink accepts multiple separate MEMORY{} blocks
 * in one script (they merge), so the MEMORY{} block below can just be
 * pasted in as its own block alongside your project's own. SECTIONS{} is
 * different -- vlink only allows ONE per script ("SECTIONS block defined
 * twice" is a real, distinct error) -- so the 32 calltblN entries below
 * need to be merged INTO your project's own single SECTIONS{} block,
 * not pasted in as a second SECTIONS{...}.
 */

MEMORY {
  calltbl_region0  (rw) : ORIGIN = 0x40, LENGTH = 2
  calltbl_region1  (rw) : ORIGIN = 0x42, LENGTH = 2
  calltbl_region2  (rw) : ORIGIN = 0x44, LENGTH = 2
  calltbl_region3  (rw) : ORIGIN = 0x46, LENGTH = 2
  calltbl_region4  (rw) : ORIGIN = 0x48, LENGTH = 2
  calltbl_region5  (rw) : ORIGIN = 0x4a, LENGTH = 2
  calltbl_region6  (rw) : ORIGIN = 0x4c, LENGTH = 2
  calltbl_region7  (rw) : ORIGIN = 0x4e, LENGTH = 2
  calltbl_region8  (rw) : ORIGIN = 0x50, LENGTH = 2
  calltbl_region9  (rw) : ORIGIN = 0x52, LENGTH = 2
  calltbl_region10 (rw) : ORIGIN = 0x54, LENGTH = 2
  calltbl_region11 (rw) : ORIGIN = 0x56, LENGTH = 2
  calltbl_region12 (rw) : ORIGIN = 0x58, LENGTH = 2
  calltbl_region13 (rw) : ORIGIN = 0x5a, LENGTH = 2
  calltbl_region14 (rw) : ORIGIN = 0x5c, LENGTH = 2
  calltbl_region15 (rw) : ORIGIN = 0x5e, LENGTH = 2
  calltbl_region16 (rw) : ORIGIN = 0x60, LENGTH = 2
  calltbl_region17 (rw) : ORIGIN = 0x62, LENGTH = 2
  calltbl_region18 (rw) : ORIGIN = 0x64, LENGTH = 2
  calltbl_region19 (rw) : ORIGIN = 0x66, LENGTH = 2
  calltbl_region20 (rw) : ORIGIN = 0x68, LENGTH = 2
  calltbl_region21 (rw) : ORIGIN = 0x6a, LENGTH = 2
  calltbl_region22 (rw) : ORIGIN = 0x6c, LENGTH = 2
  calltbl_region23 (rw) : ORIGIN = 0x6e, LENGTH = 2
  calltbl_region24 (rw) : ORIGIN = 0x70, LENGTH = 2
  calltbl_region25 (rw) : ORIGIN = 0x72, LENGTH = 2
  calltbl_region26 (rw) : ORIGIN = 0x74, LENGTH = 2
  calltbl_region27 (rw) : ORIGIN = 0x76, LENGTH = 2
  calltbl_region28 (rw) : ORIGIN = 0x78, LENGTH = 2
  calltbl_region29 (rw) : ORIGIN = 0x7a, LENGTH = 2
  calltbl_region30 (rw) : ORIGIN = 0x7c, LENGTH = 2
  calltbl_region31 (rw) : ORIGIN = 0x7e, LENGTH = 2
}

SECTIONS {
  calltbl0  0x40 : { *(calltbl0) } >calltbl_region0
  calltbl1  0x42 : { *(calltbl1) } >calltbl_region1
  calltbl2  0x44 : { *(calltbl2) } >calltbl_region2
  calltbl3  0x46 : { *(calltbl3) } >calltbl_region3
  calltbl4  0x48 : { *(calltbl4) } >calltbl_region4
  calltbl5  0x4a : { *(calltbl5) } >calltbl_region5
  calltbl6  0x4c : { *(calltbl6) } >calltbl_region6
  calltbl7  0x4e : { *(calltbl7) } >calltbl_region7
  calltbl8  0x50 : { *(calltbl8) } >calltbl_region8
  calltbl9  0x52 : { *(calltbl9) } >calltbl_region9
  calltbl10 0x54 : { *(calltbl10) } >calltbl_region10
  calltbl11 0x56 : { *(calltbl11) } >calltbl_region11
  calltbl12 0x58 : { *(calltbl12) } >calltbl_region12
  calltbl13 0x5a : { *(calltbl13) } >calltbl_region13
  calltbl14 0x5c : { *(calltbl14) } >calltbl_region14
  calltbl15 0x5e : { *(calltbl15) } >calltbl_region15
  calltbl16 0x60 : { *(calltbl16) } >calltbl_region16
  calltbl17 0x62 : { *(calltbl17) } >calltbl_region17
  calltbl18 0x64 : { *(calltbl18) } >calltbl_region18
  calltbl19 0x66 : { *(calltbl19) } >calltbl_region19
  calltbl20 0x68 : { *(calltbl20) } >calltbl_region20
  calltbl21 0x6a : { *(calltbl21) } >calltbl_region21
  calltbl22 0x6c : { *(calltbl22) } >calltbl_region22
  calltbl23 0x6e : { *(calltbl23) } >calltbl_region23
  calltbl24 0x70 : { *(calltbl24) } >calltbl_region24
  calltbl25 0x72 : { *(calltbl25) } >calltbl_region25
  calltbl26 0x74 : { *(calltbl26) } >calltbl_region26
  calltbl27 0x76 : { *(calltbl27) } >calltbl_region27
  calltbl28 0x78 : { *(calltbl28) } >calltbl_region28
  calltbl29 0x7a : { *(calltbl29) } >calltbl_region29
  calltbl30 0x7c : { *(calltbl30) } >calltbl_region30
  calltbl31 0x7e : { *(calltbl31) } >calltbl_region31
}
