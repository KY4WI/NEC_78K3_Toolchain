#pragma once

/* Register / fixed-token name tables for the NEC 78K/III core.
 *
 * Single source of truth for the text <-> field-value mapping used by
 * the disassembler's printer (OpcodeInstance.h, oi_print_reg_op() and
 * friends) and the vasm cpu backend's parse_operand()/eval_instruction().
 * Every table here was transcribed directly from OpcodeInstance.h's
 * existing, already-verified print logic -- nothing new was guessed.
 *
 * Each table is a simple {name, value} list. reg_name_lookup() does a
 * case-insensitive exact-string match and returns the field value, or -1
 * if no entry matches (so callers can tell "the text is something else"
 * from "the text is register 0").
 */

#include <string.h>
#include <strings.h>

typedef struct { const char *name; int value; } RegNameEntry;

/* OT_R1 -- 3-bit register field (MOV a,r1 / MOV r1,a families) */
static const RegNameEntry REGNAMES_R1[] = {
	{"x",0}, {"a",1}, {"c",2}, {"b",3},
	{"x'",4}, {"a'",5}, {"c'",6}, {"b'",7},
};
#define REGNAMES_R1_COUNT (sizeof(REGNAMES_R1)/sizeof(REGNAMES_R1[0]))

/* OT_R -- 4-bit register field (register-to-register MOV etc) */
static const RegNameEntry REGNAMES_R[] = {
	{"x",0}, {"a",1}, {"c",2}, {"b",3},
	{"x'",4}, {"a'",5}, {"c'",6}, {"b'",7},
	{"vpl",8}, {"vph",9}, {"upl",10}, {"uph",11},
	{"e",12}, {"d",13}, {"l",14}, {"h",15},
};
#define REGNAMES_R_COUNT (sizeof(REGNAMES_R)/sizeof(REGNAMES_R[0]))

/* OT_R2 -- 1-bit register field (a couple of C/B-only instructions) */
static const RegNameEntry REGNAMES_R2[] = {
	{"c",0}, {"b",1},
};
#define REGNAMES_R2_COUNT (sizeof(REGNAMES_R2)/sizeof(REGNAMES_R2[0]))

/* OT_RP -- 3-bit register-pair field, bank-0 group */
static const RegNameEntry REGNAMES_RP[] = {
	{"ax",0}, {"bc",1}, {"ax'",2}, {"bc'",3},
	{"vp",4}, {"up",5}, {"de",6}, {"hl",7},
};
#define REGNAMES_RP_COUNT (sizeof(REGNAMES_RP)/sizeof(REGNAMES_RP[0]))

/* OT_RP1 -- 3-bit register-pair field, alternate grouping (used by e.g.
 * INCW/DECW/register-indirect base forms). OT_RP1P is the same mapping,
 * just written wrapped in brackets, e.g. "[ax]" -- handled by the caller
 * stripping the brackets before/after using this same table. */
static const RegNameEntry REGNAMES_RP1[] = {
	{"ax",0}, {"vp",1}, {"bc",2}, {"up",3},
	{"ax'",4}, {"de",5}, {"bc'",6}, {"hl",7},
};
#define REGNAMES_RP1_COUNT (sizeof(REGNAMES_RP1)/sizeof(REGNAMES_RP1[0]))

/* OT_RP2 -- 2-bit register-pair field (only vp/up/de/hl are reachable;
 * the opcode table always masks this field to 2 bits) */
static const RegNameEntry REGNAMES_RP2[] = {
	{"vp",0}, {"up",1}, {"de",2}, {"hl",3},
};
#define REGNAMES_RP2_COUNT (sizeof(REGNAMES_RP2)/sizeof(REGNAMES_RP2[0]))

/* OT_MEM_S -- fixed short register-indirect forms (3-bit field, but only
 * values 0-5 are valid) */
static const RegNameEntry REGNAMES_MEM_S[] = {
	{"[de+]",0}, {"[hl+]",1}, {"[de-]",2}, {"[hl-]",3},
	{"[de]",4}, {"[hl]",5},
};
#define REGNAMES_MEM_S_COUNT (sizeof(REGNAMES_MEM_S)/sizeof(REGNAMES_MEM_S[0]))

/* OT_RB_NUM / OT_RB_NUM_ALT -- register bank select (SEL RBn[,ALT]) */
static const RegNameEntry REGNAMES_RB[] = {
	{"rb0",0}, {"rb1",1}, {"rb2",2}, {"rb3",3},
	{"rb4",4}, {"rb5",5}, {"rb6",6}, {"rb7",7},
};
#define REGNAMES_RB_COUNT (sizeof(REGNAMES_RB)/sizeof(REGNAMES_RB[0]))

/* Case-insensitive exact match of a length-limited string against a
 * table. Returns the field value, or -1 if nothing matches. */
static int reg_name_lookup(const RegNameEntry *tbl, size_t n,
                            const char *text, int len)
{
	size_t i;
	for (i = 0; i < n; i++) {
		if ((int)strlen(tbl[i].name) == len &&
		    strncasecmp(tbl[i].name, text, (size_t)len) == 0)
			return tbl[i].value;
	}
	return -1;
}

/* Reverse lookup (field value -> canonical name), used by anything that
 * still wants to print through this table rather than a local copy. */
static const char *reg_name_from_value(const RegNameEntry *tbl, size_t n,
                                        int value)
{
	size_t i;
	for (i = 0; i < n; i++) {
		if (tbl[i].value == value)
			return tbl[i].name;
	}
	return NULL;
}
