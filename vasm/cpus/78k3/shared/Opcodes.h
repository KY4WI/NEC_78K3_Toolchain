#pragma once

#include "Types.h"
#include "Mnemonic.h"
#include "Operand.h"
#include "Opcode.h"

/* Opcode values are hexadecimal for ISO C99 portability. Binary comments
 * retain useful bitfield and mask information for instruction encoding. */


#define OP_UNK { OT_UNK, 0, 0, 0 }

#define OP_R1_1 { OT_R1, 0, 0x07, 0 } /* field mask: 00000111 */
#define OP_R1_2 { OT_R1, 1, 0x07, 0 } /* field mask: 00000111 */

#define OP_BYTE_2 { OT_BYTE, 1, 0xFF, 0 }

#define OP_SADDR_2 { OT_SADDR, 1, 0xFF, 0 }
#define OP_BYTE_3 { OT_BYTE, 2, 0xFF, 0 }
#define OP_BYTE_4 { OT_BYTE, 3, 0xFF, 0 }

#define OP_SFR_2 { OT_SFR, 1, 0xFF, 0 }

#define OP_R_1 { OT_R, 0, 0xF0, 4 } /* field mask: 11110000 */
#define OP_R_2 { OT_R, 1, 0xF0, 4 } /* field mask: 11110000 */

#define OP_A { OT_A, 0, 0, 0 }
#define OP_X { OT_X, 0, 0, 0 }

#define OP_DE_A_P { OT_DE_A_P, 0, 0, 0 }
#define OP_DE_S_P { OT_DE_S_P, 0, 0, 0 }
#define OP_HL_A_P { OT_HL_A_P, 0, 0, 0 }
#define OP_HL_S_P { OT_HL_S_P, 0, 0, 0 }

#define OP_SADDR_3 { OT_SADDR, 2, 0xFF, 0 }

/* Two-saddr forms (MOV saddr,saddr' etc.): the source is in the 2nd
 * byte and the destination in the 3rd, both plain saddr offsets. */
#define OP_SADDR_FE_2 { OT_SADDR_FE, 1, 0xFF, 0 }

#define OP_MEM_S_1 { OT_MEM_S, 0, 0x07, 0 } /* field mask: 00000111 */

#define OP_MEM_MOD { OT_MEM, 1, 0x70, 4, true } /* field mask: 01110000 */

#define OP_SADDRP_2 { OT_SADDRP, 1, 0xFF, 0 }
#define OP_SADDRP_3 { OT_SADDRP, 2, 0xFF, 0 }

#define OP_SADDRP_FE_2 { OT_SADDRP_FE, 1, 0xFF, 0 }

#define OP_SADDRPP_2 { OT_SADDRPP, 1, 0xFF, 0 }

#define OP_ADDR16_2 { OT_ADDR16, 1, 0, 0 }
#define OP_ADDR16_3 { OT_ADDR16, 2, 0, 0 }
#define OP_ADDR16_4 { OT_ADDR16, 3, 0, 0 }

#define OP_ADDR11_1 { OT_ADDR11, 0, 0, 0 }
#define OP_ADDR5_1 { OT_ADDR5, 0, 0, 0 }

#define OP_PSWL { OT_PSWL, 0, 0, 0 }
#define OP_PSWH { OT_PSWH, 0, 0, 0 }

#define OP_SFR_3 { OT_SFR, 2, 0xFF, 0 }

#define OP_RP_1 { OT_RP, 0, 0xE0, 5 } /* field mask: 11100000 */
#define OP_RP_2 { OT_RP, 1, 0xE0, 5 } /* field mask: 11100000 */

#define OP_RP1_1 { OT_RP1, 0, 0x07, 0 } /* field mask: 00000111 */
#define OP_RP1_2 { OT_RP1, 1, 0x07, 0 } /* field mask: 00000111 */

#define OP_RP1P_2 { OT_RP1P, 1, 0x07, 0 } /* field mask: 00000111 */

#define OP_RP2_1 { OT_RP2, 0, 0x03, 0 } /* field mask: 00000011 */

#define OP_WORD_2 { OT_WORD, 1, 0, 0 }
#define OP_WORD_3 { OT_WORD, 2, 0, 0 }
#define OP_WORD_4 { OT_WORD, 3, 0, 0 }

#define OP_SFRP_2 { OT_SFRP, 1, 0xFF, 0 }
#define OP_SFRP_3 { OT_SFRP, 2, 0xFF, 0 }

#define OP_AX { OT_AX, 0, 0, 0 }
#define OP_CY { OT_CY, 0, 0, 0 }

#define OP_PSW { OT_PSW, 0, 0, 0 }
#define OP_SP { OT_SP, 0, 0, 0 }
#define OP_STBC { OT_STBC, 0, 0, 0 }
#define OP_WDM { OT_WDM, 0, 0, 0 }

#define OP_RPL_2 { OT_RPL, 1, 0x07, 0 } /* field mask: 00000111 */

#define OP_NONE { OT_NONE }

#define OP_NUM_2 { OT_NUM, 1, 0x38, 3 } /* field mask: 00111000 */

#define OP_CY_BIT_2 { OT_CY_BIT, 1, 0x07, 0 } /* field mask: 00000111 */

#define OP_N_BIT_1 { OT_N_BIT, 0, 0x07, 0 } /* field mask: 00000111 */
#define OP_N_BIT_2 { OT_N_BIT, 1, 0x07, 0 } /* field mask: 00000111 */

#define OP_ADDR16R_2 { OT_ADDR16R, 1, 0xFF, 0 }
#define OP_ADDR16R_3 { OT_ADDR16R, 2, 0xFF, 0 }
#define OP_ADDR16R_4 { OT_ADDR16R, 3, 0xFF, 0 }

#define OP_POST_BYTE_2 { OT_POST_BYTE, 1, 0xFF, 0 }
#define OP_POST_BYTE_U_2 { OT_POST_BYTE_U, 1, 0xFF, 0 }

#define OP_R2_1 { OT_R2, 0, 0x01, 0 } /* field mask: 00000001 */

#define OP_RB_NUM_2 { OT_RB_NUM, 1, 0x07, 0 } /* field mask: 00000111 */
#define OP_RB_NUM_ALT_2 { OT_RB_NUM_ALT, 1, 0x07, 0 } /* field mask: 00000111 */

static const Opcode opps[] = {
	{ M_MOV, 2, 0xB8, 0xF8, 0, 0, OP_R1_1, OP_BYTE_2 }, /* op bits: 10111000 / 11111000 */
	{ M_MOV, 3, 0x3A, 0xFF, 0, 0, OP_SADDR_2, OP_BYTE_3 },
	{ M_MOV, 3, 0x2B, 0xFF, 0, 0, OP_SFR_2, OP_BYTE_3 },
	{ M_MOV, 2, 0x24, 0xFF, 0x00, 0x08, OP_R_2, OP_R1_2 }, /* subop bits: 00000000 / 00001000 */
	{ M_MOV, 1, 0xD0, 0xF8, 0, 0, OP_A, OP_R1_1 }, /* op bits: 11010000 / 11111000 */
	{ M_MOV, 2, 0x20, 0xFF, 0, 0, OP_A, OP_SADDR_2 },
	{ M_MOV, 2, 0x22, 0xFF, 0, 0, OP_SADDR_2, OP_A },
	/* Two-operand short-direct forms (saddr,saddr / saddrp,saddrp -- this
	 * MOV plus XCH/MOVW/XCHW/ADD/ADDC/SUB/SUBC/AND/OR/XOR/CMP/ADDW/SUBW/
	 * CMPW below, 15 entries total, all sharing this exact operand order):
	 * the plain saddr/saddrp field (byte right after the opcode) is
	 * written FIRST in the mnemonic, the saddr_fe/saddrp_fe field (the
	 * next byte) SECOND -- e.g. "MOV 0xfe20, 0xfe50" encodes as
	 * 38 20 60, not 38 60 20. Confirmed against the manual's own worked
	 * example (U12117EJ2V0UM00 p.66, Figure 4-6, "MOV 0FE20H, 0FE50H"
	 * -> 0x38 0x20 0x60) and cross-checked against the real TK-840 mask
	 * ROM: with this order, every saddr,saddr instruction it contains
	 * reads as a sensible fixed-source-to-varying-destination or
	 * destination-chain pattern; the reversed order instead reads as
	 * repeatedly overwriting one cell with values it never uses. */
	{ M_MOV, 3, 0x38, 0xFF, 0, 0, OP_SADDR_3, OP_SADDR_FE_2 },
	{ M_MOV, 2, 0x10, 0xFF, 0, 0, OP_A, OP_SFR_2 },
	{ M_MOV, 2, 0x12, 0xFF, 0, 0, OP_SFR_2, OP_A },
	{ M_MOV, 1, 0x58, 0xF8, 0, 0, OP_A, OP_MEM_S_1, true }, /* op bits: 01011000 / 11111000 */
	{ M_MOV, 0, 0x00, 0xE0, 0x00, 0x8F, OP_A, OP_MEM_MOD }, /* op bits: 00000000 / 11100000; sub: 00000000 / 10001111 */
	{ M_MOV, 1, 0x50, 0xF8, 0, 0, OP_MEM_S_1, OP_A, true }, /* op bits: 01010000 / 11111000 */
	{ M_MOV, 0, 0x00, 0xE0, 0x80, 0x8F, OP_MEM_MOD, OP_A }, /* op bits: 00000000 / 11100000; sub: 10000000 / 10001111 */

	{ M_MOV, 2, 0x18, 0xFF, 0, 0, OP_A, OP_SADDRPP_2 },	/* [[]] */
	{ M_MOV, 2, 0x19, 0xFF, 0, 0, OP_SADDRPP_2, OP_A },
	{ M_MOV, 4, 0x09, 0xFF, 0xF0, 0xFF, OP_A, OP_ADDR16_3 },
	{ M_MOV, 4, 0x09, 0xFF, 0xF1, 0xFF, OP_ADDR16_3, OP_A },
	{ M_MOV, 3, 0x2B, 0xFF, 0xFE, 0xFF, OP_PSWL, OP_BYTE_3 },
	{ M_MOV, 3, 0x2B, 0xFF, 0xFF, 0xFF, OP_PSWH, OP_BYTE_3 },
	{ M_MOV, 2, 0x12, 0xFF, 0xFE, 0xFF, OP_PSWL, OP_A },
	{ M_MOV, 2, 0x12, 0xFF, 0xFF, 0xFF, OP_PSWH, OP_A },
	{ M_MOV, 2, 0x10, 0xFF, 0xFE, 0xFF, OP_A, OP_PSWL },
	{ M_MOV, 2, 0x10, 0xFF, 0xFF, 0xFF, OP_A, OP_PSWH },

	{ M_XCH, 1, 0xD8, 0xF8, 0, 0, OP_A, OP_R1_1 }, /* op bits: 11011000 / 11111000 */
	{ M_XCH, 2, 0x25, 0xFF, 0x00, 0x08, OP_R_2, OP_R1_2 }, /* subop bits: 00000000 / 00001000 */
	{ M_XCH, 0, 0x00, 0xE0, 0x04, 0x8F, OP_A, OP_MEM_MOD }, /* op bits: 00000000 / 11100000; sub: 00000100 / 10001111 */
	{ M_XCH, 2, 0x21, 0xFF, 0, 0, OP_A, OP_SADDR_2 },
	{ M_XCH, 3, 0x01, 0xFF, 0x21, 0xFF, OP_A, OP_SFR_3 },
	{ M_XCH, 2, 0x23, 0xFF, 0, 0, OP_A, OP_SADDRPP_2 },
	{ M_XCH, 3, 0x39, 0xFF, 0, 0, OP_SADDR_3, OP_SADDR_FE_2 },

	{ M_MOVW, 3, 0x60, 0xF8, 0, 0, OP_RP1_1, OP_WORD_2 }, /* op bits: 01100000 / 11111000 */
	{ M_MOVW, 4, 0x0C, 0xFF, 0, 0, OP_SADDRP_2, OP_WORD_3 },
	{ M_MOVW, 4, 0x0B, 0xFF, 0, 0, OP_SFRP_2, OP_WORD_3 },
	{ M_MOVW, 2, 0x24, 0xFF, 0x08, 0x18, OP_RP_2, OP_RP1_2 }, /* subop bits: 00001000 / 00011000 */
	{ M_MOVW, 2, 0x1C, 0xFF, 0, 0, OP_AX, OP_SADDRP_2 },
	{ M_MOVW, 2, 0x1A, 0xFF, 0, 0, OP_SADDRP_2, OP_AX },
	{ M_MOVW, 3, 0x3C, 0xFF, 0, 0, OP_SADDRP_3, OP_SADDRP_FE_2 },
	{ M_MOVW, 2, 0x11, 0xFF, 0, 0, OP_AX, OP_SFRP_2 },
	{ M_MOVW, 2, 0x13, 0xFF, 0, 0, OP_SFRP_2, OP_AX },
	{ M_MOVW, 4, 0x09, 0xFF, 0x80, 0xF8, OP_RPL_2, OP_ADDR16_3 }, /* subop bits: 10000000 / 11111000 */
	{ M_MOVW, 4, 0x09, 0xFF, 0x90, 0xF8, OP_ADDR16_3, OP_RPL_2 }, /* subop bits: 10010000 / 11111000 */

	{ M_XCHW, 2, 0x1B, 0xFF, 0, 0, OP_AX, OP_SADDRP_2 },
	{ M_XCHW, 3, 0x01, 0xFF, 0x1B, 0xFF, OP_AX, OP_SFRP_3 },
	{ M_XCHW, 3, 0x2A, 0xFF, 0, 0, OP_SADDRP_3, OP_SADDRP_FE_2 },
	{ M_XCHW, 2, 0x25, 0xFF, 0x08, 0x18, OP_RP_2, OP_RP1_2 }, /* subop bits: 00001000 / 00011000 */

	{ M_ADD, 2, 0xA8, 0xFF, 0, 0, OP_A, OP_BYTE_2 },
	{ M_ADD, 3, 0x68, 0xFF, 0, 0, OP_SADDR_2, OP_BYTE_3 },
	{ M_ADD, 4, 0x01, 0xFF, 0x68, 0xFF, OP_SFR_3, OP_BYTE_4 },
	{ M_ADD, 2, 0x88, 0xFF, 0x00, 0x08, OP_R_2, OP_R1_2 }, /* subop bits: 00000000 / 00001000 */
	{ M_ADD, 2, 0x98, 0xFF, 0, 0, OP_A, OP_SADDR_2 },
	{ M_ADD, 3, 0x01, 0xFF, 0x98, 0xFF, OP_A, OP_SFR_3 },
	{ M_ADD, 3, 0x78, 0xFF, 0, 0, OP_SADDR_3, OP_SADDR_FE_2 },
	{ M_ADD, 0, 0x00, 0xE0, 0x08, 0x8F, OP_A, OP_MEM_MOD }, /* op bits: 00000000 / 11100000; sub: 00001000 / 10001111 */
	{ M_ADD, 0, 0x00, 0xE0, 0x88, 0x8F, OP_MEM_MOD, OP_A }, /* op bits: 00000000 / 11100000; sub: 10001000 / 10001111 */

	{ M_ADDC, 2, 0xA9, 0xFF, 0, 0, OP_A, OP_BYTE_2 },
	{ M_ADDC, 3, 0x69, 0xFF, 0, 0, OP_SADDR_2, OP_BYTE_3 },
	{ M_ADDC, 4, 0x01, 0xFF, 0x69, 0xFF, OP_SFR_3, OP_BYTE_4 },
	{ M_ADDC, 2, 0x89, 0xFF, 0x00, 0x08, OP_R_2, OP_R1_2 }, /* subop bits: 00000000 / 00001000 */
	{ M_ADDC, 2, 0x99, 0xFF, 0, 0, OP_A, OP_SADDR_2 },
	{ M_ADDC, 3, 0x01, 0xFF, 0x99, 0xFF, OP_A, OP_SFR_3 },
	{ M_ADDC, 3, 0x79, 0xFF, 0, 0, OP_SADDR_3, OP_SADDR_FE_2 },
	{ M_ADDC, 0, 0x00, 0xE0, 0x09, 0x8F, OP_A, OP_MEM_MOD }, /* op bits: 00000000 / 11100000; sub: 00001001 / 10001111 */
	{ M_ADDC, 0, 0x00, 0xE0, 0x89, 0x8F, OP_MEM_MOD, OP_A }, /* op bits: 00000000 / 11100000; sub: 10001001 / 10001111 */

	{ M_SUB, 2, 0xAA, 0xFF, 0, 0, OP_A, OP_BYTE_2 },
	{ M_SUB, 3, 0x6A, 0xFF, 0, 0, OP_SADDR_2, OP_BYTE_3 },
	{ M_SUB, 4, 0x01, 0xFF, 0x6A, 0xFF, OP_SFR_3, OP_BYTE_4 },
	{ M_SUB, 2, 0x8A, 0xFF, 0x00, 0x08, OP_R_2, OP_R1_2 }, /* subop bits: 00000000 / 00001000 */
	{ M_SUB, 2, 0x9A, 0xFF, 0, 0, OP_A, OP_SADDR_2 },
	{ M_SUB, 3, 0x01, 0xFF, 0x9A, 0xFF, OP_A, OP_SFR_3 },
	{ M_SUB, 3, 0x7A, 0xFF, 0, 0, OP_SADDR_3, OP_SADDR_FE_2 },
	{ M_SUB, 0, 0x00, 0xE0, 0x0A, 0x8F, OP_A, OP_MEM_MOD }, /* op bits: 00000000 / 11100000; sub: 00001010 / 10001111 */
	{ M_SUB, 0, 0x00, 0xE0, 0x8A, 0x8F, OP_MEM_MOD, OP_A }, /* op bits: 00000000 / 11100000; sub: 10001010 / 10001111 */

	{ M_SUBC, 2, 0xAB, 0xFF, 0, 0, OP_A, OP_BYTE_2 },
	{ M_SUBC, 3, 0x6B, 0xFF, 0, 0, OP_SADDR_2, OP_BYTE_3 },
	{ M_SUBC, 4, 0x01, 0xFF, 0x6B, 0xFF, OP_SFR_3, OP_BYTE_4 },
	{ M_SUBC, 2, 0x8B, 0xFF, 0x00, 0x08, OP_R_2, OP_R1_2 }, /* subop bits: 00000000 / 00001000 */
	{ M_SUBC, 2, 0x9B, 0xFF, 0, 0, OP_A, OP_SADDR_2 },
	{ M_SUBC, 3, 0x01, 0xFF, 0x9B, 0xFF, OP_A, OP_SFR_3 },
	{ M_SUBC, 3, 0x7B, 0xFF, 0, 0, OP_SADDR_3, OP_SADDR_FE_2 },
	{ M_SUBC, 0, 0x00, 0xE0, 0x0B, 0x8F, OP_A, OP_MEM_MOD }, /* op bits: 00000000 / 11100000; sub: 00001011 / 10001111 */
	{ M_SUBC, 0, 0x00, 0xE0, 0x8B, 0x8F, OP_MEM_MOD, OP_A }, /* op bits: 00000000 / 11100000; sub: 10001011 / 10001111 */

	{ M_AND, 2, 0xAC, 0xFF, 0, 0, OP_A, OP_BYTE_2 },
	{ M_AND, 3, 0x6C, 0xFF, 0, 0, OP_SADDR_2, OP_BYTE_3 },
	{ M_AND, 4, 0x01, 0xFF, 0x6C, 0xFF, OP_SFR_3, OP_BYTE_4 },
	{ M_AND, 2, 0x8C, 0xFF, 0x00, 0x08, OP_R_2, OP_R1_2 }, /* subop bits: 00000000 / 00001000 */
	{ M_AND, 2, 0x9C, 0xFF, 0, 0, OP_A, OP_SADDR_2 },
	{ M_AND, 3, 0x01, 0xFF, 0x9C, 0xFF, OP_A, OP_SFR_3 },
	{ M_AND, 3, 0x7C, 0xFF, 0, 0, OP_SADDR_3, OP_SADDR_FE_2 },
	{ M_AND, 0, 0x00, 0xE0, 0x0C, 0x8F, OP_A, OP_MEM_MOD }, /* op bits: 00000000 / 11100000; sub: 00001100 / 10001111 */
	{ M_AND, 0, 0x00, 0xE0, 0x8C, 0x8F, OP_MEM_MOD, OP_A }, /* op bits: 00000000 / 11100000; sub: 10001100 / 10001111 */

	{ M_OR, 2, 0xAE, 0xFF, 0, 0, OP_A, OP_BYTE_2 },
	{ M_OR, 3, 0x6E, 0xFF, 0, 0, OP_SADDR_2, OP_BYTE_3 },
	{ M_OR, 4, 0x01, 0xFF, 0x6E, 0xFF, OP_SFR_3, OP_BYTE_4 },
	{ M_OR, 2, 0x8E, 0xFF, 0x00, 0x08, OP_R_2, OP_R1_2 }, /* subop bits: 00000000 / 00001000 */
	{ M_OR, 2, 0x9E, 0xFF, 0, 0, OP_A, OP_SADDR_2 },
	{ M_OR, 3, 0x01, 0xFF, 0x9E, 0xFF, OP_A, OP_SFR_3 },
	{ M_OR, 3, 0x7E, 0xFF, 0, 0, OP_SADDR_3, OP_SADDR_FE_2 },
	{ M_OR, 0, 0x00, 0xE0, 0x0E, 0x8F, OP_A, OP_MEM_MOD }, /* op bits: 00000000 / 11100000; sub: 00001110 / 10001111 */
	{ M_OR, 0, 0x00, 0xE0, 0x8E, 0x8F, OP_MEM_MOD, OP_A }, /* op bits: 00000000 / 11100000; sub: 10001110 / 10001111 */

	{ M_XOR, 2, 0xAD, 0xFF, 0, 0, OP_A, OP_BYTE_2 },
	{ M_XOR, 3, 0x6D, 0xFF, 0, 0, OP_SADDR_2, OP_BYTE_3 },
	{ M_XOR, 4, 0x01, 0xFF, 0x6D, 0xFF, OP_SFR_3, OP_BYTE_4 },
	{ M_XOR, 2, 0x8D, 0xFF, 0x00, 0x08, OP_R_2, OP_R1_2 }, /* subop bits: 00000000 / 00001000 */
	{ M_XOR, 2, 0x9D, 0xFF, 0, 0, OP_A, OP_SADDR_2 },
	{ M_XOR, 3, 0x01, 0xFF, 0x9D, 0xFF, OP_A, OP_SFR_3 },
	{ M_XOR, 3, 0x7D, 0xFF, 0, 0, OP_SADDR_3, OP_SADDR_FE_2 },
	{ M_XOR, 0, 0x00, 0xE0, 0x0D, 0x8F, OP_A, OP_MEM_MOD }, /* op bits: 00000000 / 11100000; sub: 00001101 / 10001111 */
	{ M_XOR, 0, 0x00, 0xE0, 0x8D, 0x8F, OP_MEM_MOD, OP_A }, /* op bits: 00000000 / 11100000; sub: 10001101 / 10001111 */

	{ M_CMP, 2, 0xAF, 0xFF, 0, 0, OP_A, OP_BYTE_2 },
	{ M_CMP, 3, 0x6F, 0xFF, 0, 0, OP_SADDR_2, OP_BYTE_3 },
	{ M_CMP, 4, 0x01, 0xFF, 0x6F, 0xFF, OP_SFR_3, OP_BYTE_4 },
	{ M_CMP, 2, 0x8F, 0xFF, 0x00, 0x08, OP_R_2, OP_R1_2 }, /* subop bits: 00000000 / 00001000 */
	{ M_CMP, 2, 0x9F, 0xFF, 0, 0, OP_A, OP_SADDR_2 },
	{ M_CMP, 3, 0x01, 0xFF, 0x9F, 0xFF, OP_A, OP_SFR_3 },
	{ M_CMP, 3, 0x7F, 0xFF, 0, 0, OP_SADDR_3, OP_SADDR_FE_2 },
	{ M_CMP, 0, 0x00, 0xE0, 0x0F, 0x8F, OP_A, OP_MEM_MOD }, /* op bits: 00000000 / 11100000; sub: 00001111 / 10001111 */
	{ M_CMP, 0, 0x00, 0xE0, 0x8F, 0x8F, OP_MEM_MOD, OP_A }, /* op bits: 00000000 / 11100000; sub: 10001111 / 10001111 */

	{ M_ADDW, 3, 0x2D, 0xFF, 0, 0, OP_AX, OP_WORD_2 },
	{ M_ADDW, 4, 0x0D, 0xFF, 0, 0, OP_SADDRP_2, OP_WORD_3 },
	{ M_ADDW, 5, 0x01, 0xFF, 0x0D, 0xFF, OP_SFRP_3, OP_WORD_4 },
	{ M_ADDW, 2, 0x88, 0xFF, 0x08, 0x18, OP_RP_2, OP_RP1_2 }, /* subop bits: 00001000 / 00011000 */
	{ M_ADDW, 2, 0x1D, 0xFF, 0, 0, OP_AX, OP_SADDRP_2 },
	{ M_ADDW, 3, 0x01, 0xFF, 0x1D, 0xFF, OP_AX, OP_SFRP_3 },
	{ M_ADDW, 3, 0x3D, 0xFF, 0, 0, OP_SADDRP_3, OP_SADDRP_FE_2 },

	{ M_SUBW, 3, 0x2E, 0xFF, 0, 0, OP_AX, OP_WORD_2 },
	{ M_SUBW, 4, 0x0E, 0xFF, 0, 0, OP_SADDRP_2, OP_WORD_3 },
	{ M_SUBW, 5, 0x01, 0xFF, 0x0E, 0xFF, OP_SFRP_3, OP_WORD_4 },
	{ M_SUBW, 2, 0x8A, 0xFF, 0x08, 0x18, OP_RP_2, OP_RP1_2 }, /* subop bits: 00001000 / 00011000 */
	{ M_SUBW, 2, 0x1E, 0xFF, 0, 0, OP_AX, OP_SADDRP_2 },
	{ M_SUBW, 3, 0x01, 0xFF, 0x1E, 0xFF, OP_AX, OP_SFRP_3 },
	{ M_SUBW, 3, 0x3E, 0xFF, 0, 0, OP_SADDRP_3, OP_SADDRP_FE_2 },

	{ M_CMPW, 3, 0x2F, 0xFF, 0, 0, OP_AX, OP_WORD_2 },
	{ M_CMPW, 4, 0x0F, 0xFF, 0, 0, OP_SADDRP_2, OP_WORD_3 },
	{ M_CMPW, 5, 0x01, 0xFF, 0x0F, 0xFF, OP_SFRP_3, OP_WORD_4 },
	{ M_CMPW, 2, 0x8F, 0xFF, 0x08, 0x18, OP_RP_2, OP_RP1_2 }, /* subop bits: 00001000 / 00011000 */
	{ M_CMPW, 2, 0x1F, 0xFF, 0, 0, OP_AX, OP_SADDRP_2 },
	{ M_CMPW, 3, 0x01, 0xFF, 0x1F, 0xFF, OP_AX, OP_SFRP_3 },
	{ M_CMPW, 3, 0x3F, 0xFF, 0, 0, OP_SADDRP_3, OP_SADDRP_FE_2 },

	{ M_MULU, 2, 0x05, 0xFF, 0x08, 0xF8, OP_R1_2, OP_NONE }, /* subop bits: 00001000 / 11111000 */
	{ M_DIVUW, 2, 0x05, 0xFF, 0x18, 0xF8, OP_R1_2, OP_NONE }, /* subop bits: 00011000 / 11111000 */
	{ M_MULUW, 2, 0x05, 0xFF, 0x28, 0xF8, OP_RP1_2, OP_NONE }, /* subop bits: 00101000 / 11111000 */
	{ M_DIVUX, 2, 0x05, 0xFF, 0xE8, 0xF8, OP_RP1_2, OP_NONE }, /* subop bits: 11101000 / 11111000 */

	{ M_INC, 1, 0xC0, 0xF8, 0, 0, OP_R1_1 }, /* op bits: 11000000 / 11111000 */
	{ M_INC, 2, 0x26, 0xFF, 0, 0, OP_SADDR_2 },
	{ M_DEC, 1, 0xC8, 0xF8, 0, 0, OP_R1_1 }, /* op bits: 11001000 / 11111000 */
	{ M_DEC, 2, 0x27, 0xFF, 0, 0, OP_SADDR_2 },

	{ M_INCW, 1, 0x44, 0xFC, 0, 0, OP_RP2_1 }, /* op bits: 01000100 / 11111100 */
	{ M_INCW, 3, 0x07, 0xFF, 0xE8, 0xFF, OP_SADDRP_3 },
	{ M_DECW, 1, 0x4C, 0xFC, 0, 0, OP_RP2_1 }, /* op bits: 01001100 / 11111100 */
	{ M_DECW, 3, 0x07, 0xFF, 0xE9, 0xFF, OP_SADDRP_3 },

	{ M_ROR, 2, 0x30, 0xFF, 0x40, 0xC0, OP_R1_2, OP_NUM_2 }, /* subop bits: 01000000 / 11000000 */
	{ M_ROL, 2, 0x31, 0xFF, 0x40, 0xC0, OP_R1_2, OP_NUM_2 }, /* subop bits: 01000000 / 11000000 */
	{ M_RORC, 2, 0x30, 0xFF, 0x00, 0xC0, OP_R1_2, OP_NUM_2 }, /* subop bits: 00000000 / 11000000 */
	{ M_ROLC, 2, 0x31, 0xFF, 0x00, 0xC0, OP_R1_2, OP_NUM_2 }, /* subop bits: 00000000 / 11000000 */

	{ M_SHR, 2, 0x30, 0xFF, 0x80, 0xC0, OP_R1_2, OP_NUM_2 }, /* subop bits: 10000000 / 11000000 */
	{ M_SHL, 2, 0x31, 0xFF, 0x80, 0xC0, OP_R1_2, OP_NUM_2 }, /* subop bits: 10000000 / 11000000 */
	{ M_SHRW, 2, 0x30, 0xFF, 0xC0, 0xC0, OP_RP1_2, OP_NUM_2 }, /* subop bits: 11000000 / 11000000 */
	{ M_SHLW, 2, 0x31, 0xFF, 0xC0, 0xC0, OP_RP1_2, OP_NUM_2 }, /* subop bits: 11000000 / 11000000 */

	{ M_ROR4, 2, 0x05, 0xFF, 0x88, 0xF8, OP_RP1_2, OP_NONE }, /* subop bits: 10001000 / 11111000 */
	{ M_ROL4, 2, 0x05, 0xFF, 0x98, 0xF8, OP_RP1_2, OP_NONE }, /* subop bits: 10011000 / 11111000 */

	{ M_ADJ4, 1, 0x04, 0xFF, 0, 0, OP_NONE, OP_NONE },

	{ M_MOV1, 3, 0x08, 0xFF, 0x00, 0xF8, OP_CY_BIT_2, OP_SADDR_3 }, /* subop bits: 00000000 / 11111000 */
	{ M_MOV1, 3, 0x08, 0xFF, 0x08, 0xF8, OP_CY_BIT_2, OP_SFR_3 }, /* subop bits: 00001000 / 11111000 */
	{ M_MOV1, 2, 0x03, 0xFF, 0x08, 0xF8, OP_CY_BIT_2, OP_A }, /* subop bits: 00001000 / 11111000 */
	{ M_MOV1, 2, 0x03, 0xFF, 0x00, 0xF8, OP_CY_BIT_2, OP_X }, /* subop bits: 00000000 / 11111000 */
	{ M_MOV1, 2, 0x02, 0xFF, 0x08, 0xF8, OP_CY_BIT_2, OP_PSWH },	/* (PSWL) */ /* subop bits: 00001000 / 11111000 */
	{ M_MOV1, 2, 0x02, 0xFF, 0x00, 0xF8, OP_CY_BIT_2, OP_PSWL }, /* subop bits: 00000000 / 11111000 */
	{ M_MOV1, 3, 0x08, 0xFF, 0x10, 0xF8, OP_SADDR_3, OP_CY_BIT_2 }, /* subop bits: 00010000 / 11111000 */
	{ M_MOV1, 3, 0x08, 0xFF, 0x18, 0xF8, OP_SFR_3, OP_CY_BIT_2 }, /* subop bits: 00011000 / 11111000 */
	{ M_MOV1, 2, 0x03, 0xFF, 0x18, 0xF8, OP_A, OP_CY_BIT_2 }, /* subop bits: 00011000 / 11111000 */
	{ M_MOV1, 2, 0x03, 0xFF, 0x10, 0xF8, OP_X, OP_CY_BIT_2 }, /* subop bits: 00010000 / 11111000 */
	{ M_MOV1, 2, 0x02, 0xFF, 0x18, 0xF8, OP_PSWH, OP_CY_BIT_2 }, /* subop bits: 00011000 / 11111000 */
	{ M_MOV1, 2, 0x02, 0xFF, 0x10, 0xF8, OP_PSWL, OP_CY_BIT_2 }, /* subop bits: 00010000 / 11111000 */

	{ M_AND1, 3, 0x08, 0xFF, 0x20, 0xF8, OP_CY_BIT_2, OP_SADDR_3 }, /* subop bits: 00100000 / 11111000 */
	{ M_AND1, 3, 0x08, 0xFF, 0x28, 0xF8, OP_CY_BIT_2, OP_SFR_3 }, /* subop bits: 00101000 / 11111000 */
	{ M_AND1, 2, 0x03, 0xFF, 0x28, 0xF8, OP_CY_BIT_2, OP_A }, /* subop bits: 00101000 / 11111000 */
	{ M_AND1, 2, 0x03, 0xFF, 0x20, 0xF8, OP_CY_BIT_2, OP_X}, /* subop bits: 00100000 / 11111000 */
	{ M_AND1, 2, 0x02, 0xFF, 0x28, 0xF8, OP_CY_BIT_2, OP_PSWH }, /* subop bits: 00101000 / 11111000 */
	{ M_AND1, 2, 0x02, 0xFF, 0x20, 0xF8, OP_CY_BIT_2, OP_PSWL }, /* subop bits: 00100000 / 11111000 */

	{ M_ANDI1, 3, 0x08, 0xFF, 0x30, 0xF8, OP_CY_BIT_2, OP_SADDR_3 }, /* subop bits: 00110000 / 11111000 */
	{ M_ANDI1, 3, 0x08, 0xFF, 0x38, 0xF8, OP_CY_BIT_2, OP_SFR_3 }, /* subop bits: 00111000 / 11111000 */
	{ M_ANDI1, 2, 0x03, 0xFF, 0x38, 0xF8, OP_CY_BIT_2, OP_A }, /* subop bits: 00111000 / 11111000 */
	{ M_ANDI1, 2, 0x03, 0xFF, 0x30, 0xF8, OP_CY_BIT_2, OP_X }, /* subop bits: 00110000 / 11111000 */
	{ M_ANDI1, 2, 0x02, 0xFF, 0x38, 0xF8, OP_CY_BIT_2, OP_PSWH }, /* subop bits: 00111000 / 11111000 */
	{ M_ANDI1, 2, 0x02, 0xFF, 0x30, 0xF8, OP_CY_BIT_2, OP_PSWL }, /* subop bits: 00110000 / 11111000 */

	{ M_OR1, 3, 0x08, 0xFF, 0x40, 0xF8, OP_CY_BIT_2, OP_SADDR_3 }, /* subop bits: 01000000 / 11111000 */
	{ M_OR1, 3, 0x08, 0xFF, 0x48, 0xF8, OP_CY_BIT_2, OP_SFR_3 }, /* subop bits: 01001000 / 11111000 */
	{ M_OR1, 2, 0x03, 0xFF, 0x48, 0xF8, OP_CY_BIT_2, OP_A }, /* subop bits: 01001000 / 11111000 */
	{ M_OR1, 2, 0x03, 0xFF, 0x40, 0xF8, OP_CY_BIT_2, OP_X }, /* subop bits: 01000000 / 11111000 */
	{ M_OR1, 2, 0x02, 0xFF, 0x48, 0xF8, OP_CY_BIT_2, OP_PSWH }, /* subop bits: 01001000 / 11111000 */
	{ M_OR1, 2, 0x02, 0xFF, 0x40, 0xF8, OP_CY_BIT_2, OP_PSWL }, /* subop bits: 01000000 / 11111000 */

	{ M_ORI1, 3, 0x08, 0xFF, 0x50, 0xF8, OP_CY_BIT_2, OP_SADDR_3 }, /* subop bits: 01010000 / 11111000 */
	{ M_ORI1, 3, 0x08, 0xFF, 0x58, 0xF8, OP_CY_BIT_2, OP_SFR_3 }, /* subop bits: 01011000 / 11111000 */
	{ M_ORI1, 2, 0x03, 0xFF, 0x58, 0xF8, OP_CY_BIT_2, OP_A }, /* subop bits: 01011000 / 11111000 */
	{ M_ORI1, 2, 0x03, 0xFF, 0x50, 0xF8, OP_CY_BIT_2, OP_X }, /* subop bits: 01010000 / 11111000 */
	{ M_ORI1, 2, 0x02, 0xFF, 0x58, 0xF8, OP_CY_BIT_2, OP_PSWH }, /* subop bits: 01011000 / 11111000 */
	{ M_ORI1, 2, 0x02, 0xFF, 0x50, 0xF8, OP_CY_BIT_2, OP_PSWL }, /* subop bits: 01010000 / 11111000 */

	{ M_XOR1, 3, 0x08, 0xFF, 0x60, 0xF8, OP_CY_BIT_2, OP_SADDR_3 }, /* subop bits: 01100000 / 11111000 */
	{ M_XOR1, 3, 0x08, 0xFF, 0x68, 0xF8, OP_CY_BIT_2, OP_SFR_3 }, /* subop bits: 01101000 / 11111000 */
	{ M_XOR1, 2, 0x03, 0xFF, 0x68, 0xF8, OP_CY_BIT_2, OP_A }, /* subop bits: 01101000 / 11111000 */
	{ M_XOR1, 2, 0x03, 0xFF, 0x60, 0xF8, OP_CY_BIT_2, OP_X }, /* subop bits: 01100000 / 11111000 */
	{ M_XOR1, 2, 0x02, 0xFF, 0x68, 0xF8, OP_CY_BIT_2, OP_PSWH }, /* subop bits: 01101000 / 11111000 */
	{ M_XOR1, 2, 0x02, 0xFF, 0x60, 0xF8, OP_CY_BIT_2, OP_PSWL }, /* subop bits: 01100000 / 11111000 */

	{ M_SET1, 2, 0xB0, 0xF8, 0, 0, OP_SADDR_2, OP_N_BIT_1 }, /* op bits: 10110000 / 11111000 */
	{ M_SET1, 3, 0x08, 0xFF, 0x88, 0xF8, OP_SFR_3, OP_N_BIT_2 }, /* subop bits: 10001000 / 11111000 */
	{ M_SET1, 2, 0x03, 0xFF, 0x88, 0xF8, OP_A, OP_N_BIT_2 }, /* subop bits: 10001000 / 11111000 */
	{ M_SET1, 2, 0x03, 0xFF, 0x80, 0xF8, OP_X, OP_N_BIT_2 }, /* subop bits: 10000000 / 11111000 */
	{ M_SET1, 2, 0x02, 0xFF, 0x88, 0xF8, OP_PSWH, OP_N_BIT_2 }, /* subop bits: 10001000 / 11111000 */
	{ M_SET1, 2, 0x02, 0xFF, 0x80, 0xF8, OP_PSWL, OP_N_BIT_2 }, /* subop bits: 10000000 / 11111000 */

	{ M_CLR1, 2, 0xA0, 0xF8, 0, 0, OP_SADDR_2, OP_N_BIT_1 }, /* op bits: 10100000 / 11111000 */
	{ M_CLR1, 3, 0x08, 0xFF, 0x98, 0xF8, OP_SFR_3, OP_N_BIT_2 }, /* subop bits: 10011000 / 11111000 */
	{ M_CLR1, 2, 0x03, 0xFF, 0x98, 0xF8, OP_A, OP_N_BIT_2 }, /* subop bits: 10011000 / 11111000 */
	{ M_CLR1, 2, 0x03, 0xFF, 0x90, 0xF8, OP_X, OP_N_BIT_2 }, /* subop bits: 10010000 / 11111000 */
	{ M_CLR1, 2, 0x02, 0xFF, 0x98, 0xF8, OP_PSWH, OP_N_BIT_2 }, /* subop bits: 10011000 / 11111000 */
	{ M_CLR1, 2, 0x02, 0xFF, 0x90, 0xF8, OP_PSWL, OP_N_BIT_2 }, /* subop bits: 10010000 / 11111000 */

	{ M_NOT1, 3, 0x08, 0xFF, 0x70, 0xF8, OP_SADDR_3, OP_N_BIT_2 }, /* subop bits: 01110000 / 11111000 */
	{ M_NOT1, 3, 0x08, 0xFF, 0x78, 0xF8, OP_SFR_3, OP_N_BIT_2 }, /* subop bits: 01111000 / 11111000 */
	{ M_NOT1, 2, 0x03, 0xFF, 0x78, 0xF8, OP_A, OP_N_BIT_2 }, /* subop bits: 01111000 / 11111000 */
	{ M_NOT1, 2, 0x03, 0xFF, 0x70, 0xF8, OP_X, OP_N_BIT_2 }, /* subop bits: 01110000 / 11111000 */
	{ M_NOT1, 2, 0x02, 0xFF, 0x78, 0xF8, OP_PSWH, OP_N_BIT_2 }, /* subop bits: 01111000 / 11111000 */
	{ M_NOT1, 2, 0x02, 0xFF, 0x70, 0xF8, OP_PSWL, OP_N_BIT_2 }, /* subop bits: 01110000 / 11111000 */

	{ M_SET1, 1, 0x41, 0xFF, 0, 0, OP_CY, OP_NONE },
	{ M_CLR1, 1, 0x40, 0xFF, 0, 0, OP_CY, OP_NONE },
	{ M_NOT1, 1, 0x42, 0xFF, 0, 0, OP_CY, OP_NONE },

	{ M_CALL, 3, 0x28, 0xFF, 0, 0, OP_ADDR16_2, OP_NONE },
	{ M_CALLF, 2, 0x90, 0xF8, 0, 0, OP_ADDR11_1, OP_NONE }, /* op bits: 10010000 / 11111000 */
	{ M_CALLT, 1, 0xE0, 0xE0, 0, 0, OP_ADDR5_1, OP_NONE }, /* op bits: 11100000 / 11100000 */
	/* CALL rp1 (register-direct: jump target is the value held in the
	 * register pair) vs CALL [rp1] (register-indirect: jump target is
	 * read from memory at the address the register pair holds) -- same
	 * op/subOp-prefix shape as BR's RP1/RP1P pair just above (0x05 escape,
	 * subOp bit pattern differing only in the low nibble), and BR keeps
	 * them apart with two distinct operand types (OP_RP1_2 vs OP_RP1P_2)
	 * for exactly this reason. This CALL pair used to list OP_RP1_2
	 * twice, which is not just a naming slip: with identical operand
	 * text ("call hl") on both rows, vasm's first-match-wins mnemonic
	 * search could never reach the second row at all -- confirmed by the
	 * opcode-table sweep test (see /tmp/opcode_sweep at the time this was
	 * found), which caught it precisely because the real ROM never
	 * happened to use whichever form the second row was meant to be.
	 * No ROM instance of "call [rp1]" exists to confirm this against
	 * real hardware, but the fix mirrors BR's already-ROM-verified
	 * pattern exactly. */
	{ M_CALL, 2, 0x05, 0xFF, 0x58, 0xF8, OP_RP1_2, OP_NONE }, /* subop bits: 01011000 / 11111000 */
	{ M_CALL, 2, 0x05, 0xFF, 0x78, 0xF8, OP_RP1P_2, OP_NONE }, /* subop bits: 01111000 / 11111000 */

	{ M_BRK, 1, 0x5E, 0xFF, 0, 0, OP_NONE, OP_NONE },
	{ M_RET, 1, 0x56, 0xFF, 0, 0, OP_NONE, OP_NONE },
	{ M_RETI, 1, 0x57, 0xFF, 0, 0, OP_NONE, OP_NONE },

	{ M_PUSH, 2, 0x35, 0xFF, 0, 0, OP_POST_BYTE_2, OP_NONE },
	{ M_PUSH, 1, 0x49, 0xFF, 0, 0, OP_PSW, OP_NONE },
	{ M_PUSHU, 2, 0x37, 0xFF, 0, 0, OP_POST_BYTE_U_2, OP_NONE },
	{ M_POP, 2, 0x34, 0xFF, 0, 0, OP_POST_BYTE_2, OP_NONE },
	{ M_POP, 1, 0x48, 0xFF, 0, 0, OP_PSW, OP_NONE },
	{ M_POPU, 2, 0x36, 0xFF, 0, 0, OP_POST_BYTE_U_2, OP_NONE },

	{ M_MOVW, 4, 0x0B, 0xFF, 0xFC, 0xFF, OP_SP, OP_WORD_3 },
	{ M_MOVW, 2, 0x13, 0xFF, 0xFC, 0xFF, OP_SP, OP_AX },
	{ M_MOVW, 2, 0x11, 0xFF, 0xFC, 0xFF, OP_AX, OP_SP },

	{ M_INCW, 2, 0x05, 0xFF, 0xC8, 0xFF, OP_SP, OP_NONE },
	{ M_DECW, 2, 0x05, 0xFF, 0xC9, 0xFF, OP_SP, OP_NONE },

	{ M_BR, 3, 0x2C, 0xFF, 0, 0, OP_ADDR16_2, OP_NONE },
	{ M_BR, 2, 0x05, 0xFF, 0x48, 0xF8, OP_RP1_2, OP_NONE }, /* subop bits: 01001000 / 11111000 */
	{ M_BR, 2, 0x05, 0xFF, 0x68, 0xF8, OP_RP1P_2, OP_NONE }, /* subop bits: 01101000 / 11111000 */
	{ M_BR, 2, 0x14, 0xFF, 0, 0, OP_ADDR16R_2, OP_NONE },

	{ M_BL,  2, 0x83, 0xFF, 0, 0, OP_ADDR16R_2, OP_NONE },
	{ M_BNL, 2, 0x82, 0xFF, 0, 0, OP_ADDR16R_2, OP_NONE },
	{ M_BE,  2, 0x81, 0xFF, 0, 0, OP_ADDR16R_2, OP_NONE },
	{ M_BNE, 2, 0x80, 0xFF, 0, 0, OP_ADDR16R_2, OP_NONE },
	{ M_BPE, 2, 0x85, 0xFF, 0, 0, OP_ADDR16R_2, OP_NONE },
	{ M_BPO, 2, 0x84, 0xFF, 0, 0, OP_ADDR16R_2, OP_NONE },
	{ M_BN,  2, 0x87, 0xFF, 0, 0, OP_ADDR16R_2, OP_NONE },
	{ M_BP,  2, 0x86, 0xFF, 0, 0, OP_ADDR16R_2, OP_NONE },
	{ M_BGT, 3, 0x07, 0xFF, 0xFB, 0xFF, OP_ADDR16R_3, OP_NONE },
	{ M_BGE, 3, 0x07, 0xFF, 0xF9, 0xFF, OP_ADDR16R_3, OP_NONE },
	{ M_BLT, 3, 0x07, 0xFF, 0xF8, 0xFF, OP_ADDR16R_3, OP_NONE },
	{ M_BLE, 3, 0x07, 0xFF, 0xFA, 0xFF, OP_ADDR16R_3, OP_NONE },
	{ M_BH , 3, 0x07, 0xFF, 0xFD, 0xFF, OP_ADDR16R_3, OP_NONE },
	{ M_BNH, 3, 0x07, 0xFF, 0xFC, 0xFF, OP_ADDR16R_3, OP_NONE },

	{ M_BT, 3, 0x70, 0xF8, 0, 0, OP_SADDR_2, OP_ADDR16R_3, false, OP_N_BIT_1 }, /* op bits: 01110000 / 11111000 */
	{ M_BT, 4, 0x08, 0xFF, 0xB8, 0xF8, OP_SFR_3, OP_ADDR16R_4, false, OP_N_BIT_2 }, /* subop bits: 10111000 / 11111000 */
	{ M_BT, 3, 0x03, 0xFF, 0xB8, 0xF8, OP_A, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 10111000 / 11111000 */
	{ M_BT, 3, 0x03, 0xFF, 0xB0, 0xF8, OP_X, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 10110000 / 11111000 */
	{ M_BT, 3, 0x02, 0xFF, 0xB8, 0xF8, OP_PSWH, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 10111000 / 11111000 */
	{ M_BT, 3, 0x02, 0xFF, 0xB0, 0xF8, OP_PSWL, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 10110000 / 11111000 */

	{ M_BF, 4, 0x08, 0xFF, 0xA0, 0xF8, OP_SADDR_3, OP_ADDR16R_4, false, OP_N_BIT_2 }, /* subop bits: 10100000 / 11111000 */
	{ M_BF, 4, 0x08, 0xFF, 0xA8, 0xF8, OP_SFR_3, OP_ADDR16R_4, false, OP_N_BIT_2 }, /* subop bits: 10101000 / 11111000 */
	{ M_BF, 3, 0x03, 0xFF, 0xA8, 0xF8, OP_A, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 10101000 / 11111000 */
	{ M_BF, 3, 0x03, 0xFF, 0xA0, 0xF8, OP_X, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 10100000 / 11111000 */
	{ M_BF, 3, 0x02, 0xFF, 0xA8, 0xF8, OP_PSWH, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 10101000 / 11111000 */
	{ M_BF, 3, 0x02, 0xFF, 0xA0, 0xF8, OP_PSWL, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 10100000 / 11111000 */

	{ M_BTCLR, 4, 0x08, 0xFF, 0xD0, 0xF8, OP_SADDR_3, OP_ADDR16R_4, false, OP_N_BIT_2 }, /* subop bits: 11010000 / 11111000 */
	{ M_BTCLR, 4, 0x08, 0xFF, 0xD8, 0xF8, OP_SFR_3, OP_ADDR16R_4, false, OP_N_BIT_2 }, /* subop bits: 11011000 / 11111000 */
	{ M_BTCLR, 3, 0x03, 0xFF, 0xD8, 0xF8, OP_A, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 11011000 / 11111000 */
	{ M_BTCLR, 3, 0x03, 0xFF, 0xD0, 0xF8, OP_X, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 11010000 / 11111000 */
	{ M_BTCLR, 3, 0x02, 0xFF, 0xD8, 0xF8, OP_PSWH, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 11011000 / 11111000 */
	{ M_BTCLR, 3, 0x02, 0xFF, 0xD0, 0xF8, OP_PSWL, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 11010000 / 11111000 */

	{ M_BFSET, 4, 0x08, 0xFF, 0xC0, 0xF8, OP_SADDR_3, OP_ADDR16R_4, false, OP_N_BIT_2 }, /* subop bits: 11000000 / 11111000 */
	{ M_BFSET, 4, 0x08, 0xFF, 0xC8, 0xF8, OP_SFR_3, OP_ADDR16R_4, false, OP_N_BIT_2 }, /* subop bits: 11001000 / 11111000 */
	{ M_BFSET, 3, 0x03, 0xFF, 0xC8, 0xF8, OP_A, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 11001000 / 11111000 */
	{ M_BFSET, 3, 0x03, 0xFF, 0xC0, 0xF8, OP_X, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 11000000 / 11111000 */
	{ M_BFSET, 3, 0x02, 0xFF, 0xC8, 0xF8, OP_PSWH, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 11001000 / 11111000 */
	{ M_BFSET, 3, 0x02, 0xFF, 0xC0, 0xF8, OP_PSWL, OP_ADDR16R_3, false, OP_N_BIT_2 }, /* subop bits: 11000000 / 11111000 */

	{ M_DBNZ, 2, 0x32, 0xFE, 0, 0, OP_R2_1, OP_ADDR16R_2 }, /* op bits: 00110010 / 11111110 */
	{ M_DBNZ, 3, 0x3B, 0xFF, 0, 0, OP_SADDR_2, OP_ADDR16R_3 },

	{ M_BRKCS, 2, 0x05, 0xFF, 0xD8, 0xF8, OP_RB_NUM_2, }, /* subop bits: 11011000 / 11111000 */
	{ M_RETCS, 3, 0x29, 0xFF, 0, 0, OP_ADDR16_2 },

	{ M_MOVM , 2, 0x15, 0xFF, 0x00, 0xFF, OP_DE_A_P, OP_A },
	{ M_MOVM , 2, 0x15, 0xFF, 0x10, 0xFF, OP_DE_S_P, OP_A },
	{ M_MOVBK, 2, 0x15, 0xFF, 0x20, 0xFF, OP_DE_A_P, OP_HL_A_P },
	{ M_MOVBK, 2, 0x15, 0xFF, 0x30, 0xFF, OP_DE_S_P, OP_HL_S_P },
	{ M_XCHM , 2, 0x15, 0xFF, 0x01, 0xFF, OP_DE_A_P, OP_A },
	{ M_XCHM , 2, 0x15, 0xFF, 0x11, 0xFF, OP_DE_S_P, OP_A },
	{ M_XCHBK, 2, 0x15, 0xFF, 0x21, 0xFF, OP_DE_A_P, OP_HL_A_P },
	{ M_XCHBK, 2, 0x15, 0xFF, 0x31, 0xFF, OP_DE_S_P, OP_HL_S_P },

	{ M_CMPME  , 2, 0x15, 0xFF, 0x04, 0xFF, OP_DE_A_P, OP_A },
	{ M_CMPME  , 2, 0x15, 0xFF, 0x14, 0xFF, OP_DE_S_P, OP_A },
	{ M_CMPBKE , 2, 0x15, 0xFF, 0x24, 0xFF, OP_DE_A_P, OP_HL_A_P },
	{ M_CMPBKE , 2, 0x15, 0xFF, 0x34, 0xFF, OP_DE_S_P, OP_HL_S_P },
	{ M_CMPMNE , 2, 0x15, 0xFF, 0x05, 0xFF, OP_DE_A_P, OP_A },
	{ M_CMPMNE , 2, 0x15, 0xFF, 0x15, 0xFF, OP_DE_S_P, OP_A },
	{ M_CMPBKNE, 2, 0x15, 0xFF, 0x25, 0xFF, OP_DE_A_P, OP_HL_A_P },
	{ M_CMPBKNE, 2, 0x15, 0xFF, 0x35, 0xFF, OP_DE_S_P, OP_HL_S_P },
	{ M_CMPMC  , 2, 0x15, 0xFF, 0x07, 0xFF, OP_DE_A_P, OP_A },
	{ M_CMPMC  , 2, 0x15, 0xFF, 0x17, 0xFF, OP_DE_S_P, OP_A },
	{ M_CMPBKC , 2, 0x15, 0xFF, 0x27, 0xFF, OP_DE_A_P, OP_HL_A_P },
	{ M_CMPBKC , 2, 0x15, 0xFF, 0x37, 0xFF, OP_DE_S_P, OP_HL_S_P },
	{ M_CMPMNC , 2, 0x15, 0xFF, 0x06, 0xFF, OP_DE_A_P, OP_A },
	{ M_CMPMNC , 2, 0x15, 0xFF, 0x16, 0xFF, OP_DE_S_P, OP_A },
	{ M_CMPBKNC, 2, 0x15, 0xFF, 0x26, 0xFF, OP_DE_A_P, OP_HL_A_P },
	{ M_CMPBKNC, 2, 0x15, 0xFF, 0x36, 0xFF, OP_DE_S_P, OP_HL_S_P },

	{ M_MOV, 4, 0x09, 0xFF, 0x44, 0xFF, OP_STBC, OP_BYTE_4 },
	{ M_MOV, 4, 0x09, 0xFF, 0x42, 0xFF, OP_WDM, OP_BYTE_4 },

	{ M_SWRS, 1, 0x43, 0xFF, 0, 0 },

	{ M_SEL, 2, 0x05, 0xFF, 0xA8, 0xF8, OP_RB_NUM_2 }, /* subop bits: 10101000 / 11111000 */
	{ M_SEL, 2, 0x05, 0xFF, 0xB8, 0xF8, OP_RB_NUM_ALT_2 }, /* subop bits: 10111000 / 11111000 */

	{ M_NOP, 1, 0x00, 0xFF, 0, 0 },
	{ M_EI, 1, 0x4B, 0xFF, 0, 0 },
	{ M_DI, 1, 0x4A, 0xFF, 0, 0 },
};

#define OPPS_COUNT (sizeof(opps) / sizeof(opps[0]))
