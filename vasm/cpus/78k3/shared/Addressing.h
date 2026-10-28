#pragma once

/* Short-direct (saddr/saddrp) and special-function-register (sfr/sfrp)
 * address math for the NEC 78K/III core.
 *
 * This is the SINGLE SOURCE OF TRUTH for these address<->byte transforms,
 * shared by both the disassembler (decode: byte -> address, for display)
 * and the vasm cpu backend (encode: address -> byte, for assembly). A
 * correction made here applies to both tools the next time they're
 * rebuilt, so "what the disassembler shows" and "what the assembler
 * would produce" can't silently drift apart.
 *
 * All addresses are plain 16-bit CPU addresses (e.g. 0xFE20-0xFF1FH for
 * saddr, 0xFF00-0xFFFFH for sfr). All byte values are the raw 8-bit
 * offset field taken directly out of the instruction encoding.
 */

#include "Types.h"

/* ---- saddr (OT_SADDR / OT_SADDRP / OT_SADDRPP, and the destination of
 * the two-saddr forms, which sits in the instruction's third byte) ----
 * Offsets 20H-FFH are FE20H-FEFFH, offsets 00H-1FH are FF00H-FF1FH.
 * Range: FE20H-FF1FH, byte 0x00-0xFF. */
static uint16 addr_from_saddr_byte(byte b)
{
	uint16 w = b;
	if (b < 0x20)
		w += 0xff00;
	else
		w += 0xfe00;
	return w;
}

/* Inverse of addr_from_saddr_byte(). addr must be in FE20H-FF1FH; the
 * caller is responsible for range-checking (the assembler side should
 * reject/flag an out-of-range saddr operand rather than silently
 * truncating it). */
static byte saddr_byte_from_addr(uint16 addr)
{
	if (addr >= 0xff00)
		return (byte)(addr - 0xff00);
	else
		return (byte)(addr - 0xfe00);
}

/* ---- saddr_fe (the "_FE" field: OT_SADDR_FE / OT_SADDRP_FE, the source
 * operand of the two-saddr forms MOV saddr,saddr' and friends) ----
 * Same mapping as plain saddr. The source offset is the instruction's
 * second byte and the destination's the third. Confirmed against real
 * code: mask ROM 179BH (38 26 2C) copies panel_rx_byte FE26H to FE2CH,
 * and TK-941 AB00 C17FH (38 F0 29) loads eeprom_addr FE29H from X inside
 * a 16-byte EEPROM write loop. An earlier bit-5/4 swap of this field is
 * gone: no code supports it. */
static uint16 addr_from_saddr_fe_byte(byte b)
{
	return addr_from_saddr_byte(b);
}

static byte saddr_fe_byte_from_addr(uint16 addr)
{
	return saddr_byte_from_addr(addr);
}

/* ---- sfr / sfrp (special function register addressing, FF00H-FFFFH) ----
 * Confirmed via the manual's Figure 4-7: a straight, ungated copy of the
 * offset byte into the low 8 bits of FF00H (no XOR network here -- only
 * the saddr window has that quirk, per the manual's own text). */
static uint16 addr_from_sfr_byte(byte b)
{
	return (uint16)(0xff00u | b);
}

static byte sfr_byte_from_addr(uint16 addr)
{
	return (byte)(addr & 0xff);
}
