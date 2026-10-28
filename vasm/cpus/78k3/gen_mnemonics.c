/* gen_mnemonics.c - build-time generator for mnemonics_gen.c
 *
 * Reads the SAME opcode table the disassembler uses (shared/Opcodes.h)
 * and mechanically emits vasm's `mnemonic mnemonics[]` initializer from
 * it, one entry per opps[] row whose operand types are all ones the
 * cpu.c backend actually knows how to encode/decode generically.
 *
 * This is the anti-drift measure the whole "share one opcode table"
 * design was for: nobody hand-transcribes opcode bytes, masks or
 * operand-type combinations into a second table for the assembler.
 * Opcodes.h is edited once (e.g. to fix an addressing bug, or to add a
 * previously-OP_UNK instruction) and both tools pick it up on rebuild.
 *
 * Run via ./regen.sh when the shared opcode table changes -- this is
 * a standalone host program that produces the checked-in
 * mnemonics_gen.c, which is included by cpu.c. The normal toolchain
 * build uses the checked-in generated table without regeneration.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "shared/Types.h"
#include "shared/Operand.h"
#include "shared/Mnemonic.h"
#include "shared/Opcode.h"
#include "shared/Opcodes.h"

/* Operand types the cpu.c backend does NOT yet handle generically.
 * Any opps[] row using one of these on any operand slot is left out of
 * mnemonics[] for now -- see the TODO list this generator prints to
 * stderr, and cpu.c's parse_operand()/eval_instruction() comments. */
static int is_unsupported_type(int t)
{
	switch (t) {
	/* OT_MEM: "based addressing" (mod=0x06, [reg+byte]) IS now supported
	 * by cpu.c's parse_operand()/encode_operand() -- the other three
	 * OT_MEM sub-modes (indexed/register-indirect/based-indexed) are
	 * not yet, but that's a per-instance runtime distinction (which
	 * bracket syntax was actually written), not something this
	 * per-row, build-time table can express -- see cpu.c's OT_MEM
	 * comments for the real coverage boundary. */
	/* OT_RPL (MOVW rpl,!addr16 / MOVW !addr16,rpl) is now supported --
	 * the register mapping was already confirmed (identical table to
	 * OT_RP1; see cpu.c's header comment and OpcodeInstance.h's OT_RPL
	 * comment), the only thing missing was a parse_operand()/
	 * encode_operand() case, which cpu.c now has (it just routes OT_RPL
	 * through the same reg_table_for()/write_masked() path as OT_RP1). */
	/* OT_CY_BIT/OT_N_BIT (bit instructions: MOV1/AND1/OR1/ANDI1/ORI1/XOR1/
	 * SET1/CLR1/NOT1/BT/BF/BTCLR/BFSET) ARE now supported, despite their
	 * position-dependent PRINTING in OpcodeInstance.h (the disassembler
	 * attaches a bit number as a ".N" suffix onto whichever operand ends
	 * up printed last, cy-side or register-side depending on the row) --
	 * that's purely how the disassembler chooses to DISPLAY the decoded
	 * bytes, not a constraint on what text vasm's assembler has to ACCEPT.
	 * cpu.c gives each its own self-contained syntax that needs no
	 * cross-operand-slot state during parsing, fitting the generic
	 * per-slot model after all: OT_N_BIT is a plain bare-expression operand
	 * (its own comma field, same shape as OT_NUM -- "set1 saddr_expr,
	 * bit_expr", not the disassembler's fused "saddr_expr.bit_expr"), and
	 * OT_CY_BIT is "cy.N" (the literal token "cy" plus a dot-suffixed bit
	 * expression, entirely self-contained in ONE comma field, since the
	 * CY-paired mnemonics' opcode rows never have a separate OT_N_BIT slot
	 * to hold it otherwise). See cpu.c's parse_operand() OT_CY_BIT comment
	 * for the full reasoning, including why matching the disassembler's
	 * exact cosmetic form was not worth the added parser complexity. */
	/* OT_POST_BYTE/_U (PUSH/POP register-list bitmask) is now supported --
	 * cpu.c's parse_operand() accepts a brace-delimited, comma-separated
	 * register list ("{ax,bc,de,hl}") as a single operand field (cpu.h's
	 * START_PARENTH/END_PARENTH treat {}  like () so the std syntax
	 * module doesn't split it on the internal commas) and assembles it
	 * into the post-byte value per the NEC manual's Figure 5-1 register
	 * mapping; encode_operand() writes it out like any other register-
	 * value operand. See cpu.c's parse_operand()/encode_operand() OT_POST_BYTE
	 * comments for the register-name-to-bit table. */
	case OT_RB_NUM_ALT:    /* SEL RBn,ALT -- the disassembler prints this
	                         * with a literal ",alt" suffix (OpcodeInstance.h),
	                         * but OT_RB_NUM_ALT shares its bare register-name
	                         * table (rb0..rb7) with plain OT_RB_NUM, so a
	                         * generic per-slot register-table match can
	                         * never tell "sel rb3" from the ALT row -- the
	                         * plain SEL row (tried first, identical text)
	                         * always wins. Confirmed via the opcode sweep
	                         * test that this row was fully unreachable even
	                         * before this exclusion (silently shadowed
	                         * rather than erroring); excluding it here just
	                         * makes that gap explicit and stable across any
	                         * future opps[] reordering, matching how the
	                         * other excluded categories are handled. Needs a
	                         * dedicated two-operand (register + fixed "alt"
	                         * keyword) or suffix-aware parse branch, and
	                         * ideally a real datasheet/ROM instance to
	                         * confirm the intended syntax against -- no ROM
	                         * instance of this instruction exists to check. */
	case OT_UNK:
		return 1;
	default:
		return 0;
	}
}

static int row_supported(const Opcode *o)
{
	if (is_unsupported_type(o->operand1.opType)) return 0;
	if (is_unsupported_type(o->operand2.opType)) return 0;
	if (is_unsupported_type(o->operand3.opType)) return 0;
	/* bytes==0 is the OT_MEM "figure the length out from the addressing
	 * sub-mode actually used" placeholder (cpu.c's opcode_bytes()) --
	 * no longer excluded now that OT_MEM's mod=0x06 case is supported. */
	return 1;
}

typedef struct { int idx; char name[16]; } Entry;

/* vasm's new_inst() (atom.c) commits to the FIRST candidate mnemonic row
 * whose operands parse successfully -- it never backtracks to try a
 * second row once one has matched. Several mnemonics have more than one
 * row that can textually match the SAME operand text but encode it
 * differently (almost always: a short, more specific form vs. a longer,
 * more general fallback), so row order controls which encoding actually
 * comes out, and it must match what the real ROM (and any real 78K3
 * assembler) prefers: the shortest/most specific encoding that fits.
 * Two concrete cases so far:
 *   - BR !addr16 (3-byte absolute) vs BR $addr16 (2-byte PC-relative) --
 *     same bare hex-address syntax either way; cpu.c's parse_operand()
 *     rejects (PO_NOMATCH) an OT_ADDR16R target outside relative range,
 *     so trying OT_ADDR16R first and OT_ADDR16 second reproduces
 *     "relative if it fits, else absolute" exactly.
 *   - MOV A,r1 (1-byte, operand1 pinned to the fixed keyword "a") vs
 *     MOV r,r1 (2-byte, operand1 a full 16-entry register field that
 *     ALSO happens to include "a") -- textually identical for "mov a,b";
 *     the fixed-keyword row has to be tried first or the general
 *     register-table row always wins and the ROM's actual 1-byte
 *     encoding is never produced.
 * The two cases above share the same shape: a row whose operand1 is a
 * narrow/fixed type should be tried before a row whose operand1 is a
 * broader type that happens to overlap it. specificity_rank() scores
 * operand1's type from narrowest (0) to broadest (highest), and
 * cmp_entry() sorts on that after the mnemonic name, before falling back
 * to original table order. shared/Opcodes.h's own row order is left
 * alone -- it's fine as-is for the disassembler, which decodes by
 * matching the actual opcode byte, never by array position. */
static int specificity_rank(int opType)
{
	switch (opType) {
	/* zero-width fixed keyword tokens: match one exact spelling only */
	case OT_A: case OT_A_S: case OT_AX: case OT_CY:
	case OT_PSW: case OT_PSWL: case OT_PSWH: case OT_SP:
	case OT_STBC: case OT_WDM: case OT_X:
	case OT_DE_A_P: case OT_DE_S_P: case OT_HL_A_P: case OT_HL_S_P:
		return 0;
	case OT_RP1P:
		return 1;
	/* Every register-name-table type ranks ahead of every bare
	 * address/immediate-expression type (ranks 4+ below), full stop.
	 * cpu.c's parse_expr() will cheerfully treat a bare identifier like
	 * "bc" or "ax" as a (so far unresolved) external symbol reference --
	 * indistinguishable, syntactically, from a genuine forward label --
	 * so if a register-table row for this same text exists, it must get
	 * first crack, or the bare-expression row commits to a phantom
	 * "undefined symbol" match and only fails much later, confusingly,
	 * at encode time. (cpu.c's parse_addr_expr() adds a second, belt-
	 * and-suspenders check for this at parse time too.) */
	case OT_R1: case OT_R2: case OT_RP: case OT_RP1: case OT_RP2:
	case OT_MEM_S: case OT_RB_NUM: case OT_RB_NUM_ALT:
		return 2;
	/* broadest register set -- includes "a"/"x"/etc alongside every
	 * other register, so it must lose to any of the above when they
	 * also match */
	case OT_R:
		return 3;
	/* PC-relative branch target: parse-time range check falls through
	 * to the absolute form (rank below) when out of range */
	case OT_ADDR16R:
		return 4;
	/* range-restricted absolute-address forms: same bare-hex-address
	 * syntax as OT_ADDR16, but only for a narrower window (saddr:
	 * FE20H-FF1FH, saddr_fe the same) -- e.g. "mov 0xfe2a, a" matches
	 * both this and OT_ADDR16's "mov !addr16, a" row identically as
	 * text. Like OT_ADDR16R above, cpu.c's parse_operand() rejects
	 * (PO_NOMATCH) an out-of-window address here so it falls through to
	 * OT_ADDR16 when the target isn't reachable via the short form. */
	case OT_SADDR: case OT_SADDR_FE: case OT_SADDRP: case OT_SADDRP_FE:
		return 5;
	case OT_ADDR16:
		return 6;
	default:
		return 7;
	}
}

static int cmp_entry(const void *pa, const void *pb)
{
	const Entry *a = pa, *b = pb;
	int c = strcmp(a->name, b->name);
	if (c) return c;
	c = specificity_rank(opps[a->idx].operand1.opType) -
	    specificity_rank(opps[b->idx].operand1.opType);
	if (c) return c;
	return a->idx - b->idx;  /* stable: preserve opps[] order otherwise */
}

int main(void)
{
	size_t n = OPPS_COUNT;
	Entry *entries = malloc(n * sizeof(Entry));
	size_t cnt = 0, i;
	size_t excluded = 0;

	for (i = 0; i < n; i++) {
		const Opcode *o = &opps[i];
		if (!row_supported(o)) { excluded++; continue; }

		const char *upper = mnemonic_names[(int)o->mnemonic];
		size_t j, len = strlen(upper);
		if (len >= sizeof(entries[0].name)) len = sizeof(entries[0].name) - 1;
		for (j = 0; j < len; j++)
			entries[cnt].name[j] = (char)tolower((unsigned char)upper[j]);
		entries[cnt].name[len] = '\0';
		entries[cnt].idx = (int)i;
		cnt++;
	}

	qsort(entries, cnt, sizeof(Entry), cmp_entry);

	printf("/* AUTO-GENERATED by gen_mnemonics.c from shared/Opcodes.h.\n");
	printf(" * DO NOT EDIT BY HAND -- edit shared/Opcodes.h and rebuild instead.\n");
	printf(" * %zu of %zu opcode-table rows included; %zu excluded (unsupported\n",
	       cnt, n, excluded);
	printf(" * addressing modes -- see gen_mnemonics.c's is_unsupported_type()). */\n");
	printf("/* NOTE: deliberately no #include \"vasm.h\" / \"shared/Operand.h\" here --\n");
	printf(" * this file is #include'd directly from cpu.c AFTER both of those are\n");
	printf(" * already included there, and vasm.h has no include guard, so including\n");
	printf(" * them again here would redefine every struct/enum in vasm.h. */\n\n");
	printf("mnemonic mnemonics[] = {\n");
	for (i = 0; i < cnt; i++) {
		const Opcode *o = &opps[entries[i].idx];
		printf("  {\"%s\", {%d,%d,%d}, {%d}},\n",
		       entries[i].name,
		       (int)o->operand1.opType, (int)o->operand2.opType, (int)o->operand3.opType,
		       entries[i].idx);
	}
	printf("};\n\n");
	printf("const int mnemonic_cnt = sizeof(mnemonics)/sizeof(mnemonics[0]);\n");

	fprintf(stderr, "gen_mnemonics: %zu/%zu opcode rows included, %zu excluded "
	        "(unsupported addressing modes -- "
	        "OT_RB_NUM_ALT)\n", cnt, n, excluded);
	return 0;
}
