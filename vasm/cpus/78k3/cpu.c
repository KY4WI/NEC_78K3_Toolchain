/* cpu.c - NEC 78K/III (uPD78312A) cpu-description file for vasm
 *
 * Instruction encoder for NEC 78K/III microcontrollers. See cpu.h for
 * the overall design note: this backend does not maintain its own copy
 * of the instruction set. The mnemonic table (mnemonics[]) is generated
 * by gen_mnemonics.c straight from shared/Opcodes.h -- the exact same
 * table nec78k3_disassembler.c decodes with -- and every address-math
 * helper (saddr/saddr_fe/sfr <-> byte) comes from shared/Addressing.h,
 * again shared verbatim with the disassembler. A fix made in either
 * shared header applies to both tools next rebuild.
 *
 * COVERAGE (see gen_mnemonics.c's stderr summary for the exact count):
 * every opps[] row is included EXCEPT rows using:
 *   - OT_RB_NUM_ALT           SEL RBn,ALT -- shares its bare register-name
 *                             table with plain OT_RB_NUM (SEL RBn), so a
 *                             generic per-slot match can never reach it;
 *                             needs a dedicated two-operand or suffix-aware
 *                             form, and a real syntax reference to confirm
 *                             against (no ROM instance of it exists).
 * These are TODOs, not silent gaps: gen_mnemonics.c's excluded rows
 * simply don't appear in mnemonics[], so assembling one produces vasm's
 * normal "instruction not supported by cpu" error rather than silently
 * emitting wrong bytes.
 *
 * OT_POST_BYTE/_U (PUSH/POP/PUSHU/POPU's register-list "post byte") IS
 * included. Syntax is a brace-delimited, comma-separated register list --
 * "push {ax,bc,de,hl}" / "pop {ax,bc,de,hl}" -- rather than bare top-level
 * comma-separated operands, because MAX_OPERANDS==3 can't hold the up to
 * 6 names the list may contain; cpu.h's START_PARENTH/END_PARENTH count
 * '{'/'}' as parens so the std syntax module hands the whole list to
 * parse_operand() as a single operand field (same trick the ARM backend
 * uses). See parse_operand()'s OT_POST_BYTE case below for the exact
 * register-name-to-bit mapping (NEC 78K/III Instructions User's Manual,
 * Figure 5-1) and encode_operand() for how the assembled byte is written
 * out. PUSH PSW / POP PSW / RETI use the separate, already-working
 * OT_PSW fixed-token path -- PSW cannot appear inside an ordinary PUSH/
 * POP's register list (only PUSHU/POPU's, at the slot ordinary PUSH/POP
 * gives to UP).
 *
 * OT_RPL (MOVW rpl,!addr16 / MOVW !addr16,rpl) IS included as of this
 * pass. It was excluded earlier only because parse_operand()/
 * encode_operand() had no case for it -- the register mapping itself was
 * never in question: it's the identical 3-bit Q2Q1Q0 field, same byte
 * position and mask, as OT_RP1 (confirmed against the manual's rp1
 * tables -- see OpcodeInstance.h's OT_RPL comment, which the disassembler
 * side has relied on all along). It's wired up below by simply routing
 * OT_RPL through the same register-table lookup and the same
 * write_masked() encode path as OT_RP1, rather than adding any new
 * machinery -- see reg_table_for() and encode_operand()'s register-table
 * case.
 *
 * OT_MEM ("based/indexed/register-indirect/based-indexed" addressing)
 * IS included, all four sub-modes: register indirect (mod 16H: [de+],
 * [hl-], [vp], ...), based indexed (17H: [de+a], [hl+b], [vp+de], ...),
 * based (06H: [de+n], [sp+n], ...) and indexed (0AH: [addr16+de],
 * [addr16+a], ...). See parse_operand()'s OT_MEM comment.
 *
 * OT_N_BIT / OT_CY_BIT (bit instructions: SET1/CLR1/NOT1/BT/BF/BTCLR/
 * BFSET/MOV1/AND1/OR1/ANDI1/ORI1/XOR1) ARE included. The opcode rows carry
 * the bit as an operand of its own ("set1 saddr_expr,bit_expr",
 * "bt saddr_expr,label,bit_expr") or attached to CY ("mov1 cy.3,0xfe30",
 * "andi1 cy.3,0xfe30"). NEC's own form, the bit after the byte it belongs
 * to ("set1 flags.3", "bt flags.3,label", "mov1 cy,flags.3",
 * "and1 cy,/flags.3"), is accepted too: cpu_fixup_bit_syntax() rewrites
 * it into the row form before the operands are matched. Both range-check
 * to 0-7 at encode time. See parse_operand()'s OT_CY_BIT comment.
 */

#include "vasm.h"

#include "shared/Types.h"
#include "shared/Operand.h"
#include "shared/Mnemonic.h"
#include "shared/Opcode.h"
#include "shared/Opcodes.h"
#include "shared/Addressing.h"
#include "shared/RegisterNames.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

/* Independent backend version; updated when the 78K/III backend is released. */
#define VASM78K3_VERSION "0.1"
const char *cpu_copyright =
  "vasm 78k3 cpu backend " VASM78K3_VERSION " (c) 2026 Dave (KY4WI)"
  " -- NEC 78K/III (uPD78312A)";
const char *cpuname = "78k3";
int bytespertaddr = 2;

/* mnemonic mnemonics[]; const int mnemonic_cnt; -- auto-generated from
 * shared/Opcodes.h, see gen_mnemonics.c. Regenerate with ./regen.sh
 * after any change to shared/Opcodes.h. */
#include "mnemonics_gen.c"

/* ================= operand parsing ================= */

operand *new_operand(void)
{
  operand *op = mymalloc(sizeof(*op));
  op->type = -2;  /* unset */
  op->reg = 0;
  op->val = NULL;
  return op;
}

/* fixed single-keyword operands with no encoded value at all (the
 * register/mode is implied entirely by which opcode row matched) */
typedef struct { int type; const char *text; } FixedTok;
static const FixedTok fixed_tokens[] = {
  {OT_A,"a"}, {OT_A_S,"as"}, {OT_AX,"ax"}, {OT_CY,"cy"},
  {OT_PSW,"psw"}, {OT_PSWL,"pswl"}, {OT_PSWH,"pswh"}, {OT_SP,"sp"},
  {OT_X,"x"}, {OT_STBC,"stbc"}, {OT_WDM,"wdm"},
  {OT_DE_A_P,"[de+]"}, {OT_DE_S_P,"[de-]"}, {OT_HL_A_P,"[hl+]"}, {OT_HL_S_P,"[hl-]"},
};
#define FIXED_TOKENS_COUNT (sizeof(fixed_tokens)/sizeof(fixed_tokens[0]))

static int is_fixed_token_type(int type)
{
  size_t i;
  for (i = 0; i < FIXED_TOKENS_COUNT; i++)
    if (fixed_tokens[i].type == type)
      return 1;
  return 0;
}

static int match_fixed_token(const char *text, int len, int type)
{
  size_t i;
  for (i = 0; i < FIXED_TOKENS_COUNT; i++) {
    if (fixed_tokens[i].type == type &&
        (int)strlen(fixed_tokens[i].text) == len &&
        strncasecmp(fixed_tokens[i].text, text, (size_t)len) == 0)
      return 1;
  }
  return 0;
}

/* which register-name table (see shared/RegisterNames.h) an operand
 * type uses, if any. OT_RP1P is handled separately (needs bracket
 * stripping) even though it shares OT_RP1's table. OT_RPL shares
 * OT_RP1's table outright (see this file's header comment -- confirmed
 * identical Q-code field, not just a similar one). */
static const RegNameEntry *reg_table_for(int type, size_t *n)
{
  switch (type) {
  case OT_R1:  *n = REGNAMES_R1_COUNT;  return REGNAMES_R1;
  case OT_R:   *n = REGNAMES_R_COUNT;   return REGNAMES_R;
  case OT_R2:  *n = REGNAMES_R2_COUNT;  return REGNAMES_R2;
  case OT_RP:  *n = REGNAMES_RP_COUNT;  return REGNAMES_RP;
  case OT_RP1: case OT_RPL: *n = REGNAMES_RP1_COUNT; return REGNAMES_RP1;
  case OT_RP2: *n = REGNAMES_RP2_COUNT; return REGNAMES_RP2;
  case OT_MEM_S: *n = REGNAMES_MEM_S_COUNT; return REGNAMES_MEM_S;
  case OT_RB_NUM: case OT_RB_NUM_ALT: *n = REGNAMES_RB_COUNT; return REGNAMES_RB;
  default: *n = 0; return NULL;
  }
}

static int trimmed_len(char *p, int len)
{
  while (len > 0 && isspace((unsigned char)p[len - 1]))
    len--;
  return len;
}

/* Whether c could plausibly start a vasm expression (a number, a quoted
 * character constant, an identifier/symbol reference, a unary op, or a
 * parenthesized sub-expression). Used to reject obviously-wrong operand
 * text (starting with '[', for instance) BEFORE calling parse_expr() --
 * see parse_expr_full()'s comment for why that matters. */
static int looks_like_expr_start(char c)
{
  return isalnum((unsigned char)c) || c == '_' || c == '$' ||
         c == '\'' || c == '"' || c == '+' || c == '-' || c == '~' ||
         c == '!' || c == '(';
}

/* parse_expr(&pp) stops at the first character it can't extend the
 * expression with -- it happily parses "sfr" out of "sfr[0x10]" as a
 * (probably undefined) symbol reference and leaves "[0x10]" sitting
 * unread. Used bare, that silently turns a wrong-looking operand into a
 * bogus match (PO_MATCH with an unresolvable symbol) instead of the
 * PO_NOMATCH that should send vasm on to try the next candidate mnemonic
 * row. Worse, when the text doesn't look like an expression at all (e.g.
 * it starts with '['), parse_expr's own primary_expr() calls
 * general_error() directly as a side effect before failing -- printing a
 * real error even though we only wanted a quiet "doesn't match, try the
 * next candidate mnemonic". So: refuse up front (no parse_expr call at
 * all) unless the text looks like it could be an expression, and even
 * then require the expression to consume the text exactly to its end.
 * Every plain (non-bracketed, non-#immediate) expression operand below
 * goes through this wrapper instead of calling parse_expr directly. */
static expr *parse_expr_full(char *p, int tlen)
{
  char *pp;
  expr *tree;

  if (tlen < 1 || !looks_like_expr_start(p[0]))
    return NULL;
  pp = p;
  tree = parse_expr(&pp);
  if (!tree)
    return NULL;
  /* parse_expr() also consumes any blanks after the expression, while
   * tlen has already had them trimmed off. So for "#1 ; comment" the
   * operand text is "1 " and pp lands one past the blank -- without
   * stepping back over it, every operand followed by a space and a
   * ';' comment was rejected ("illegal operand types" on instructions,
   * "invalid data operand" on .byte/.word). */
  while (pp > p && isspace((unsigned char)pp[-1]))
    pp--;
  if (pp != p + tlen)
    return NULL;
  return tree;
}

/* Whether text (p,len) exactly spells one of our register names or fixed
 * keywords (in ANY table -- not just the one the current candidate row
 * asked for). parse_expr will cheerfully treat a bare identifier like
 * "bc" or "ax" as a (so far unresolved) external symbol reference, since
 * syntactically it's indistinguishable from a genuine forward-referenced
 * label. When some OTHER row's operand slot for this same mnemonic
 * actually wants an address/immediate and it's this text that's being
 * offered, and the expression doesn't resolve to a real value, this is
 * the safety net: reject rather than let a phantom "undefined symbol
 * named bc" match win the row and then fail later, at encode time, with
 * a confusing "illegal operand" -- long after the CORRECT row (matched
 * via one of the actual register tables) was already passed over. */
static int looks_like_register_name(const char *p, int len)
{
  static const struct { const RegNameEntry *tbl; size_t n; } tables[] = {
    {REGNAMES_R1, REGNAMES_R1_COUNT},
    {REGNAMES_R, REGNAMES_R_COUNT},
    {REGNAMES_R2, REGNAMES_R2_COUNT},
    {REGNAMES_RP, REGNAMES_RP_COUNT},
    {REGNAMES_RP1, REGNAMES_RP1_COUNT},
    {REGNAMES_RP2, REGNAMES_RP2_COUNT},
    {REGNAMES_MEM_S, REGNAMES_MEM_S_COUNT},
    {REGNAMES_RB, REGNAMES_RB_COUNT},
  };
  size_t i;
  for (i = 0; i < sizeof(tables) / sizeof(tables[0]); i++)
    if (reg_name_lookup(tables[i].tbl, tables[i].n, (char *)p, len) >= 0)
      return 1;
  for (i = 0; i < FIXED_TOKENS_COUNT; i++)
    if ((int)strlen(fixed_tokens[i].text) == len &&
        strncasecmp(fixed_tokens[i].text, p, (size_t)len) == 0)
      return 1;
  return 0;
}

/* Shared logic for every "plain address/immediate expression" operand
 * branch below: parse the text, and if it doesn't resolve to a constant
 * right now, refuse the match when the same text also happens to spell a
 * register name (see looks_like_register_name() above) rather than
 * accept a phantom undefined-symbol match. `resolved` reports whether
 * `*value` came from a genuine constant evaluation, so callers that need
 * a range check (saddr, addr16r) know whether one is possible yet. */
static expr *parse_addr_expr(char *p, int tlen, section *sec, taddr pc,
                              int *resolved, taddr *value)
{
  expr *tree = parse_expr_full(p, tlen);
  *resolved = 0;
  if (!tree)
    return NULL;
  if (eval_expr(tree, value, sec, pc)) {
    *resolved = 1;
    return tree;
  }
  if (looks_like_register_name(p, tlen))
    return NULL;  /* phantom undefined-symbol match -- try another row */
  return tree;    /* genuine (probably forward-referenced) unresolved label */
}

int parse_operand(char *p, int len, operand *op, int requires)
{
  char *orig = p;
  int tlen;

  p = skip(p);
  tlen = len - (int)(p - orig);
  tlen = trimmed_len(p, tlen);

  if (requires == OP_DATA_ABS) {
    expr *tree = parse_expr_full(p, tlen);
    if (!tree)
      return PO_NOMATCH;
    op->type = OP_DATA_ABS;
    op->val = tree;
    return PO_MATCH;
  }

  /* fixed keyword operands (a, ax, cy, psw, [de+], stbc, wdm, ...) */
  if (is_fixed_token_type(requires)) {
    if (match_fixed_token(p, tlen, requires)) {
      op->type = requires;
      op->reg = 0;
      op->val = NULL;
      return PO_MATCH;
    }
    return PO_NOMATCH;
  }

  /* [rp1] bracketed indirect form -- same register table as OT_RP1,
   * just written wrapped in brackets */
  if (requires == OT_RP1P) {
    int val;
    if (tlen < 2 || p[0] != '[' || p[tlen - 1] != ']')
      return PO_NOMATCH;
    val = reg_name_lookup(REGNAMES_RP1, REGNAMES_RP1_COUNT, p + 1, tlen - 2);
    if (val < 0)
      return PO_NOMATCH;
    op->type = requires;
    op->reg = val;
    op->val = NULL;
    return PO_MATCH;
  }

  /* PUSH/POP/PUSHU/POPU register list -- "{ax,bc,de,hl}" -- assembled into
   * the post byte per NEC 78K/III Instructions User's Manual p.94,
   * Figure 5-1 ("8-bit Data that Specifies the Register Pair which
   * Performs Stack Operations"): the figure's own lead lines run
   * bit0=RP0(AX), bit1=RP1(BC), bit2=RP2(AX,RSS=1), bit3=RP3(BC,RSS=1),
   * bit4=RP4(VP), bit5=RP5(UP)/PSW, bit6=RP6(DE), bit7=RP7(HL) -- confirmed
   * directly against the rendered page (the "bit7...bit0" header labels the
   * box columns left to right, but each lead line still runs from its own
   * bit down to its own register, and RP0/AX is the line out of bit0, not
   * bit7). This matches the disassembler's own OT_POST_BYTE table in
   * OpcodeInstance.h exactly (also independently re-derived from the same
   * figure). A prior version of this table here had every bit mirrored
   * (ax/hl and bc/de swapped, 7-N instead of N) -- harmless for vbcc's own
   * codegen, which only ever emits the symmetric "push/pop {ax,bc,de,hl}"
   * (whose bit SET happens to be identical either way round), but would
   * have silently assembled any other combination -- e.g. "push {vp,up}"
   * -- to the wrong byte, or a PUSHU/POPU list naming PSW to the wrong one
   * entirely. Fixed to match the manual and the disassembler.
   * This project's own code never uses the RSS=1 shadow bank, but bits
   * 2/3 are still accepted (see push_regs below) so disassembled code
   * that does use them reassembles. cpu.h's START_PARENTH/END_PARENTH make sure
   * this whole braced list arrives here as a single operand field, not
   * pre-split on its internal commas. */
  if (requires == OT_POST_BYTE || requires == OT_POST_BYTE_U) {
    /* Bits 2/3 (RP2/RP3: AX/BC with RSS=1) are accepted both as the
     * disassembler prints them ("ax'", "bc'") and as the manual's own
     * example writes them ("PUSH AX, BC, RP2, RP3", p.223), so any list
     * the disassembler prints assembles back to the same post byte. */
    static const struct { const char *name; int bit; } push_regs[] = {
      {"ax",0}, {"bc",1}, {"vp",4}, {"up",5}, {"de",6}, {"hl",7},
      {"ax'",2}, {"bc'",3}, {"rp2",2}, {"rp3",3}
    };
    static const struct { const char *name; int bit; } pushu_regs[] = {
      {"ax",0}, {"bc",1}, {"vp",4}, {"psw",5}, {"de",6}, {"hl",7},
      {"ax'",2}, {"bc'",3}, {"rp2",2}, {"rp3",3}
    };
    const int use_u = (requires == OT_POST_BYTE_U);
    const size_t tbl_n = 10;
    unsigned mask = 0;
    char *q, *listend;

    if (tlen < 3 || p[0] != '{' || p[tlen - 1] != '}')
      return PO_NOMATCH;
    q = p + 1;
    listend = p + tlen - 1;
    if (q >= listend)
      return PO_NOMATCH;  /* "{}" -- empty list */

    while (q < listend) {
      char *itemstart;
      int itemlen;
      size_t i;
      int found = 0;

      while (q < listend && isspace((unsigned char)*q))
        q++;
      itemstart = q;
      while (q < listend && *q != ',')
        q++;
      itemlen = (int)(q - itemstart);
      while (itemlen > 0 && isspace((unsigned char)itemstart[itemlen - 1]))
        itemlen--;
      if (itemlen <= 0)
        return PO_NOMATCH;  /* empty item, e.g. "{ax,,hl}" or trailing comma */

      for (i = 0; i < tbl_n; i++) {
        const char *name = use_u ? pushu_regs[i].name : push_regs[i].name;
        int bit = use_u ? pushu_regs[i].bit : push_regs[i].bit;
        if ((int)strlen(name) == itemlen &&
            strncasecmp(name, itemstart, (size_t)itemlen) == 0) {
          unsigned bitval = 1u << bit;
          if (mask & bitval)
            return PO_NOMATCH;  /* same register named twice */
          mask |= bitval;
          found = 1;
          break;
        }
      }
      if (!found)
        return PO_NOMATCH;  /* unknown/unsupported register name in list */

      if (q < listend && *q == ',')
        q++;
    }

    op->type = requires;
    op->reg = (int)mask;
    op->val = NULL;
    return PO_MATCH;
  }

  /* register-name-table operands (r, r1, r2, rp, rp1, rp2, mem_s, rb) */
  {
    size_t n;
    const RegNameEntry *tbl = reg_table_for(requires, &n);
    if (tbl) {
      int val = reg_name_lookup(tbl, n, p, tlen);
      if (val < 0)
        return PO_NOMATCH;
      op->type = requires;
      op->reg = val;
      op->val = NULL;
      return PO_MATCH;
    }
  }

  /* [addr] bracketed forms: saddrpp ([saddrp]) and addr5 ([0x00xx]) */
  if (requires == OT_SADDRPP || requires == OT_ADDR5) {
    expr *tree;
    if (tlen < 2 || p[0] != '[' || p[tlen - 1] != ']')
      return PO_NOMATCH;
    tree = parse_expr_full(p + 1, tlen - 2);
    if (!tree)
      return PO_NOMATCH;
    op->type = requires;
    op->val = tree;
    return PO_MATCH;
  }

  /* OT_MEM: the four memory addressing modes that share one opcode row
   * (NEC 78K/III User's Manual, section 4.2), written as the disassembler
   * prints them:
   *   mod 16H  register indirect  [de+] [hl+] [de-] [hl-] [de] [hl] [vp] [up]
   *   mod 17H  based indexed      [de+a] [hl+a] [de+b] [hl+b] [vp+de] [vp+hl]
   *   mod 06H  based              [de+n] [sp+n] [hl+n] [up+n] [vp+n]  (n: byte)
   *   mod 0AH  indexed            [addr16+de] [addr16+a] [addr16+hl] [addr16+b]
   * op->reg holds (mod << 8) | register field; the instruction's size
   * follows from mod (2, 2, 3 or 4 bytes; see mem_mode_bytes()). Blanks
   * inside the brackets are ignored. Mode 06H was verified against ROM
   * instances (xch a,[de+0x05] -> 06 04 05; mov a,[hl+0x01] -> 06 20 01)
   * and the datasheet (AND A,[VP+10H] -> 06 4C 10); 16H/17H/0AH against
   * the mask ROM's own instances through the disassembler round trip. */
  if (requires == OT_MEM) {
    static const char *const m16[8] = { "de+", "hl+", "de-", "hl-", "de", "hl", "vp", "up" };
    static const char *const m17[6] = { "de+a", "hl+a", "de+b", "hl+b", "vp+de", "vp+hl" };
    static const char *const m06[5] = { "de", "sp", "hl", "up", "vp" };
    static const char *const m0a[4] = { "de", "a", "hl", "b" };
    char in[128];
    int n = 0, i, k;
    char *plus;
    expr *tree;

    if (tlen < 3 || p[0] != '[' || p[tlen - 1] != ']')
      return PO_NOMATCH;
    for (i = 1; i < tlen - 1 && n < (int)sizeof(in) - 1; i++)
      if (p[i] != ' ' && p[i] != '\t')
        in[n++] = p[i];
    in[n] = '\0';
    for (k = 0; k < 8; k++)
      if (strcasecmp(in, m16[k]) == 0) {
        op->type = requires; op->reg = (0x16 << 8) | k; op->val = NULL;
        return PO_MATCH;
      }
    for (k = 0; k < 6; k++)
      if (strcasecmp(in, m17[k]) == 0) {
        op->type = requires; op->reg = (0x17 << 8) | k; op->val = NULL;
        return PO_MATCH;
      }
    /* [reg+n]: a based register, then the offset expression */
    plus = strchr(in, '+');
    if (plus) {
      for (k = 0; k < 5; k++)
        if ((int)strlen(m06[k]) == (int)(plus - in) &&
            strncasecmp(in, m06[k], (size_t)(plus - in)) == 0)
          break;
      if (k < 5) {
        tree = parse_expr_full(plus + 1, (int)strlen(plus + 1));
        if (!tree)
          return PO_NOMATCH;
        op->type = requires; op->reg = (0x06 << 8) | k; op->val = tree;
        return PO_MATCH;
      }
    }
    /* [addr16+reg]: the expression, then the index register after the
     * last '+' */
    plus = strrchr(in, '+');
    if (plus && plus > in) {
      for (k = 0; k < 4; k++)
        if (strcasecmp(plus + 1, m0a[k]) == 0)
          break;
      if (k < 4) {
        *plus = '\0';   /* the expression parser reads to the end */
        tree = parse_expr_full(in, (int)(plus - in));
        if (!tree)
          return PO_NOMATCH;
        op->type = requires; op->reg = (0x0a << 8) | k; op->val = tree;
        return PO_MATCH;
      }
    }
    return PO_NOMATCH;
  }

  /* #immediate forms: byte, word */
  if (requires == OT_BYTE || requires == OT_WORD) {
    char *start;
    expr *tree;
    if (tlen < 1 || p[0] != '#')
      return PO_NOMATCH;
    start = skip(p + 1);
    tree = parse_expr_full(start, tlen - (int)(start - p));
    if (!tree)
      return PO_NOMATCH;
    op->type = requires;
    op->val = tree;
    return PO_MATCH;
  }

  /* sfr[expr] / sfr[expr].w -- OT_SFR/OT_SFRP, matching the disassembler's
   * own print format exactly (OpcodeInstance.h prints "sfr[0x%02x]" and
   * "sfr[0x%02x].w" for the unlabeled case). The bracketed value is just
   * the low byte, as printed; encode_operand() ORs in 0xff00 if the
   * evaluated constant comes in under 0x100, so both "sfr[0x4e]" and a
   * full "sfr[0xff4e]" resolve to the same SFR. */
  if (requires == OT_SFR || requires == OT_SFRP) {
    int wlen = tlen;
    expr *tree;
    if (requires == OT_SFRP && wlen >= 2 &&
        p[wlen - 2] == '.' && (p[wlen - 1] == 'w' || p[wlen - 1] == 'W'))
      wlen -= 2;
    if (wlen < 5 || strncasecmp(p, "sfr[", 4) != 0 || p[wlen - 1] != ']')
      return PO_NOMATCH;
    tree = parse_expr_full(p + 4, wlen - 5);
    if (!tree)
      return PO_NOMATCH;
    op->type = requires;
    op->val = tree;
    return PO_MATCH;
  }

  /* cy.N -- OT_CY_BIT, the "which bit" side of the CY-paired bit
   * instructions (MOV1/AND1/OR1/ANDI1/ORI1/XOR1). These mnemonics move a
   * single bit between the carry flag and a numbered bit of another
   * operand (saddr/sfr/a/x/pswh/pswl -- see shared/Opcodes.h's OP_CY_BIT_2
   * rows), but the opcode table only ever gives this instruction FAMILY
   * two real operand slots: one for "cy" itself, one for the OTHER side
   * (saddr/sfr/register) -- there is no separate third slot for the bit
   * number anywhere in these rows (unlike SET1/CLR1/NOT1/BT/BF/BTCLR/
   * BFSET below, which DO have a genuine standalone OT_N_BIT slot). Yet
   * OP_CY_BIT_2's own byteOffset/mask (shared/Opcodes.h) is real encoding
   * metadata for a bit-number field -- so the bit number has to travel
   * somewhere, and the only operand slot available to carry it is this
   * one, textually attached to "cy" itself.
   *
   * The disassembler (OpcodeInstance.h's oi_print_operand()) instead
   * prints the bit suffix attached to the OTHER operand at the end of the
   * line ("cy, saddr.3", not "cy.3, saddr") -- a print-time cosmetic
   * choice, not a hard syntax requirement, and reproducing it here would
   * need cross-operand-slot state during a single row's parse (the
   * saddr-side parse_operand() call stashing a bit value for the later
   * cy-side call to pick up, or vice versa) that this backend's per-slot
   * parse_operand()/encode_operand() dispatch has no mechanism for and
   * that every other operand type here deliberately avoids needing.
   * "cy.N" keeps the bit value entirely local to CY_BIT's own operand
   * text/slot instead -- e.g. "mov1 cy.3,0xfe30" or "mov1 0xfe30,cy.3" --
   * a real, working, unambiguous syntax, just not disassembler-identical
   * for this one instruction family; a human transcribing real disassembly
   * needs to move the ".N" over to "cy" by hand. Confirmed as the only
   * operand slot with room for the bit value by reading every OP_CY_BIT_2
   * row in shared/Opcodes.h: none of them pair with a separate OT_N_BIT
   * slot in the same row. */
  if (requires == OT_CY_BIT) {
    expr *tree;
    int dot;
    if (tlen < 4 || strncasecmp(p, "cy", 2) != 0 || p[2] != '.')
      return PO_NOMATCH;
    dot = 2;
    tree = parse_expr_full(p + dot + 1, tlen - dot - 1);
    if (!tree)
      return PO_NOMATCH;
    op->type = requires;
    op->val = tree;
    return PO_MATCH;
  }

  /* saddrp / saddrp_fe: plain address, with an optional trailing ".w"
   * (the disassembler always prints one, e.g. "0xfe20.w") -- strip it
   * before requiring full consumption of the rest. Routed through
   * parse_addr_expr() so a bare register name (e.g. "bc" in "addw ax,
   * bc", where operand2 needs OT_RP1, not OT_SADDRP) doesn't get
   * mistaken for a forward-referenced address symbol -- see
   * looks_like_register_name()'s comment. */
  if (requires == OT_SADDRP || requires == OT_SADDRP_FE) {
    int wlen = tlen;
    int resolved;
    taddr val;
    expr *tree;
    if (wlen >= 2 && p[wlen - 2] == '.' &&
        (p[wlen - 1] == 'w' || p[wlen - 1] == 'W'))
      wlen -= 2;
    tree = parse_addr_expr(p, wlen, NULL, 0, &resolved, &val);
    if (!tree)
      return PO_NOMATCH;
    /* Same fix as OT_SADDR/OT_SADDR_FE above, and for the same reason:
     * a saddrp byte is a nonlinear transform of the address
     * (saddr_byte_from_addr()/saddr_fe_byte_from_addr()), not
     * relocatable_type()-eligible, so an unresolved (forward/cross-
     * section) symbol here can never actually be encoded as saddrp --
     * reject now rather than fail confusingly at encode time. OT_RPL
     * (MOVW rp1,!addr16 / MOVW !addr16,rp1) is now included (see this
     * file's header comment) and -- unlike saddrp above -- its !addr16
     * operand goes through the ordinary OT_ADDR16 relocation path below,
     * so it CAN take an unresolved/forward-referenced address. Whether
     * machines/78k3/machine.c's load_word()/store_word() are updated to
     * emit it instead of their current two-8-bit-access decomposition is
     * a separate codegen decision, not an assembler limitation anymore. */
    if (!resolved)
      return PO_NOMATCH;
    {
      taddr lo = 0xfe20;
      taddr hi = 0xff1e;
      if (val < lo || val > hi || (val & 1))
        return PO_NOMATCH;
    }
    op->type = requires;
    op->val = tree;
    return PO_MATCH;
  }

  /* addr16r: PC-relative branch displacement. Only BR has both an
   * OT_ADDR16R row and a same-slot OT_ADDR16 (absolute) row for the same
   * mnemonic -- gen_mnemonics.c's cmp_entry() sorts the ADDR16R one first
   * for exactly this reason -- so if the target is resolvable now and
   * turns out to be outside relative range, reject with PO_NOMATCH here
   * (instead of a hard encode-time error) so vasm falls through and tries
   * the absolute row next, matching how the real ROM always prefers the
   * short relative encoding when it fits. `total` (this instruction's own
   * eventual byte count) isn't known yet at parse time -- every ADDR16R
   * row is 2, 3 or 4 bytes -- so 2 (the common case, all the plain
   * Bxx/BR/BL/... conditional branches) is used as an estimate; being off
   * by 1-2 bytes only matters for a target within a byte or two of the
   * +-127 edge, and encode_operand()'s own exact check is still the
   * authority that actually raises cpu_error(4) if this guess was wrong.
   *
   * A leading '!' must be rejected here outright, BEFORE any of that --
   * found via the disassembler repo's stress test, the same way as the
   * OT_ADDR16 '!'-strip fix below (05b0939) was: "br !0x9abc" (intended
   * as the absolute-address form -- the manual's own marker for "this is
   * an absolute addr16, not relative") instead matched THIS row first
   * (it's tried before OT_ADDR16, per the comment above), and
   * parse_addr_expr() -> vasm's generic expression grammar parsed the
   * leading '!' as C-style logical-NOT, silently resolving to target=0.
   * Since 0 trivially disp's to something in -128..127 range from
   * nearly any PC, the out-of-range fallthrough above never triggered,
   * so the (wrong) relative encoding was accepted instead of falling
   * through to OT_ADDR16's absolute row with the real address. '!' is
   * never valid syntax for the relative ($addr16r) form in the first
   * place -- the manual marks that form with '$', never '!' -- so
   * there's no legitimate relative-branch meaning being given up here:
   * rejecting immediately just routes "!..." straight to the absolute
   * row, which already strips the '!' and encodes it correctly. */
  if (requires == OT_ADDR16R) {
    int resolved;
    taddr target;
    expr *tree;
    if (tlen > 0 && p[0] == '!')
      return PO_NOMATCH;  /* '!' always means the absolute form -- see above */
    tree = parse_addr_expr(p, tlen, current_section,
                            current_section ? current_section->pc : 0,
                            &resolved, &target);
    if (!tree)
      return PO_NOMATCH;
    /* No range check here. This used to reject a resolvable target whose
     * displacement, estimated as if every branch were 2 bytes long, fell
     * outside -128..127. The estimate is exact for BR and the 2-byte
     * conditionals, but BT/BF/BTCLR/BFSET and DBNZ saddr are 3 or 4
     * bytes, so a real, in-range branch at displacement +126/+127 was
     * rejected -- and with no other row to fall back to, failed with
     * "illegal operand types" (found by round-tripping disassembled
     * random images). The check isn't needed anyway: every pass,
     * br_row_for_pass() (BR -> absolute form), cbranch_needs_relax()
     * (conditional invert-and-jump) and the DBNZ trampoline re-test the
     * exact displacement using each row's real byte count, for constant
     * targets as well as forward labels. So accept, and let those
     * decide. (`resolved`/`target` are still filled in by the parse.) */
    (void)resolved;
    (void)target;
    op->type = requires;
    op->val = tree;
    return PO_MATCH;
  }

  /* saddr / saddr_fe: same bare-hex-address syntax as OT_ADDR16, just a
   * narrower window (both FE20H-FF1FH) -- e.g.
   * "mov 0xfe2a, a" matches both this and an OT_ADDR16 row identically as
   * text ("mov !addr16, a"). Same pattern as OT_ADDR16R above: reject
   * (PO_NOMATCH) here if the address is resolvable now and outside the
   * window, so the search falls through to the OT_ADDR16 row -- matching
   * the ROM, which always prefers the short saddr encoding when the
   * address is reachable that way.
   *
   * Unlike OT_ADDR16R, this ALSO rejects when the address is not yet
   * resolvable at all (a forward-referenced or cross-section/external
   * label -- e.g. a vbcc-emitted "mov a,_counter" referencing a .bss
   * symbol vlink hasn't placed yet). relocatable_type() below only
   * covers OT_WORD/OT_ADDR16/OT_ADDR16R: a saddr byte is
   * saddr_byte_from_addr(addr), a nonlinear transform of the real
   * address, which a linker relocation cannot express (it can only
   * patch in S+A). Accepting the match here optimistically -- as this
   * used to do -- commits to a row that can never actually be encoded
   * once linking is genuinely required, and only fails much later, at
   * encode_operand() time, with a confusing "illegal operand" (found by
   * actually testing a vbcc-emitted cross-section reference, not
   * assumed). Rejecting here instead falls through to OT_ADDR16, which
   * handles the resolved-later case correctly via a real relocation. */
  if (requires == OT_SADDR || requires == OT_SADDR_FE) {
    int resolved;
    taddr target;
    expr *tree = parse_addr_expr(p, tlen, NULL, 0, &resolved, &target);
    if (!tree)
      return PO_NOMATCH;
    if (!resolved)
      return PO_NOMATCH;  /* can't ever be relocated as saddr -- try OT_ADDR16 instead */
    {
      taddr lo = 0xfe20;
      taddr hi = 0xff1f;
      if (target < lo || target > hi)
        return PO_NOMATCH;  /* out of window -- try OT_ADDR16 instead */
    }
    op->type = requires;
    op->val = tree;
    return PO_MATCH;
  }

  /* plain expression forms: addr16, addr11, num, n_bit */
  switch (requires) {
  case OT_ADDR16:
  {
    /* NEC 78K/III syntax writes an absolute 16-bit address as "!addr16"
     * (e.g. the manual's "MOV A, !addr16" row) -- '!' there is purely a
     * syntactic marker for "this is an absolute address, not a
     * saddr/relative one", not an operator. vasm's generic expression
     * grammar, however, treats a leading '!' as the C-style logical-NOT
     * unary operator, so without this special case "!0x1234" silently
     * parsed as "logical-not(0x1234)" == 0 -- found via the
     * disassembler repo's stress test: "mov a, !0x1234" and
     * "mov !0x5678, a" both assembled to an address field of 0x0000,
     * with no error. Strip exactly one leading '!' here (ONLY for this
     * operand type -- OT_SADDR below deliberately does not get this
     * treatment, so "!0xfe30", written to force the absolute encoding
     * of an address that also happens to be in saddr range, still
     * works as intended instead of being silently reinterpreted as
     * saddr-range zero) before handing the rest to the ordinary
     * expression parser. */
    char *q = p;
    int qlen = tlen;
    int resolved;
    taddr val;
    expr *tree;
    if (qlen > 0 && q[0] == '!') { q++; qlen--; }
    tree = parse_addr_expr(q, qlen, NULL, 0, &resolved, &val);
    if (!tree)
      return PO_NOMATCH;
    op->type = requires;
    op->val = tree;
    return PO_MATCH;
  }
  case OT_ADDR11:
  case OT_NUM: case OT_N_BIT:
  {
    int resolved;
    taddr val;
    expr *tree;
    /* The manual writes CALLF's operand as "!addr11"; strip the marker
     * for the same reason as OT_ADDR16 above (otherwise it parses as
     * logical NOT and the target becomes 0). */
    if (requires == OT_ADDR11 && tlen > 0 && p[0] == '!') { p++; tlen--; }
    tree = parse_addr_expr(p, tlen, NULL, 0, &resolved, &val);
    if (!tree)
      return PO_NOMATCH;
    op->type = requires;
    op->val = tree;
    return PO_MATCH;
  }
  default:
    break;
  }

  return PO_NOMATCH;
}

/* ================= encoding ================= */

/* Write `val`'s low bits into outbytes[byteOffset] at the given
 * opMask/opShift, OR'd on top of whatever fixed opcode bits are already
 * there (the fixed pattern never overlaps a real operand field's bits,
 * by construction of the shared opcode table). */
static void write_masked(byte *outbytes, const Operand *od, unsigned val)
{
  outbytes[od->byteOffset] = (byte)(outbytes[od->byteOffset] |
                                     ((val << od->opShift) & od->opMask));
}

static void write_word_le(byte *outbytes, int byteOffset, uint16 w)
{
  outbytes[byteOffset] = (byte)(w & 0xff);
  outbytes[byteOffset + 1] = (byte)((w >> 8) & 0xff);
}

/* Evaluate a parsed operand's expression to a constant. V1 only
 * supports single-file assembly with locally-defined labels (matching
 * the project's current scope -- a self-contained label/entries bundle
 * per shared/Addressing.h and the disassembler's -l/-t files), so an
 * expression that isn't resolvable yet is an error rather than a
 * relocation. */
static int eval_const(expr *tree, section *sec, taddr pc, taddr *result)
{
  return eval_expr(tree, result, sec, pc);
}

/* Encode one already-matched operand into outbytes. total is the
 * instruction's final size in bytes (needed for OT_ADDR16R's pc-relative
 * math, matching the disassembler's `pc + op->bytes + (signed)disp`
 * formula exactly). Returns 0 on success, non-zero if a cpu_error() was
 * already raised. */
/* Relocatable operand types: cross-file (or otherwise not-yet-resolvable)
 * symbol references that vlink can patch at link time, because their
 * final value is a plain affine function of the resolved symbol address
 * (S+A, or S+A-P for the pc-relative case) -- vlink's generic REL_ABS/
 * REL_PC relocation types cover that exactly, no 78k3-specific relocation
 * type needed. SADDR/SADDR_FE/SFR are deliberately NOT here: their address
 * math (shared/Addressing.h) is a conditional branch before truncating
 * to a byte, not an affine transform, so a generic
 * linker relocation can't express them -- those still hard-error on an
 * unresolvable symbol, same as before this existed. */
static int relocatable_type(int opType)
{
  switch (opType) {
  case OT_WORD: case OT_ADDR16: case OT_ADDR16R:
    return 1;
  default:
    return 0;
  }
}

/* forward decl: real definition (with its full rationale) sits below,
 * next to br_fits_relative() which is its other caller -- encode_operand()
 * just also needs it a little earlier in file order. */
static int local_label_disp(operand *p, section *sec, taddr pc,
                             taddr total, taddr *disp);

/* extra_off: how far `outbytes` has already been shifted from the start
 * of the instruction's own dblock (0 for an ordinary, unrelaxed
 * instruction, where outbytes IS the dblock's base). A relocation's
 * recorded byteOffset (add_extnreloc()'s last argument) must be relative
 * to the DBLOCK's start, not to whatever sub-pointer this particular call
 * was given -- od->byteOffset alone is only correct when the two
 * coincide. The relaxation trampolines below (eval_instruction()'s
 * `relax`/`relax_dbnz` blocks) call encode_operand() with outbytes
 * already advanced past the short instruction(s) that precede the
 * absolute-BR piece, so they must pass that same advance here too, or
 * the relocation silently patches the wrong bytes (confirmed: without
 * this, a same-file-but-not-yet-resolvable target in one of those
 * trampolines patched bytes back inside the preceding short
 * instruction instead of its own 2-byte address field). Plain writes
 * (write_masked, outbytes[...]) don't need this -- they already go
 * through the (correctly pre-shifted) outbytes pointer itself. */
static int encode_operand(operand *p, const Operand *od, byte *outbytes,
                           taddr total, section *sec, taddr pc,
                           rlist **relocs, taddr extra_off)
{
  taddr val;

  if (!p || od->opType == OT_NONE)
    return 0;

  /* fixed-keyword and register-table operands just contribute their
   * (already-parsed) register/field number */
  if (is_fixed_token_type(od->opType)) {
    return 0;  /* nothing to encode -- the opcode row IS the operand */
  }
  switch (od->opType) {
  case OT_R1: case OT_R: case OT_R2: case OT_RP: case OT_RP1: case OT_RPL:
  case OT_RP1P: case OT_RP2: case OT_MEM_S: case OT_RB_NUM: case OT_RB_NUM_ALT:
  case OT_POST_BYTE: case OT_POST_BYTE_U:
    /* p->reg already holds the fully-assembled post byte (parse_operand()'s
     * OT_POST_BYTE/_U case); OP_POST_BYTE_2/_U_2 (shared/Opcodes.h) place it
     * at byteOffset=1 with a full 0xff mask, same as any other single-byte
     * register-value operand. */
    write_masked(outbytes, od, (unsigned)p->reg);
    return 0;

  case OT_MEM:
  {
    /* mod goes in byte0's low 5 bits (every OP_MEM_MOD row's fixed
     * op/opMask only constrains byte0's top 3 bits), the register field in
     * od's own slot (byte1 bits 4-6), and any offset or address right after
     * that byte. See parse_operand()'s OT_MEM comment for the modes. */
    int mod = p->reg >> 8;
    taddr off;
    outbytes[0] = (byte)(outbytes[0] | mod);
    write_masked(outbytes, od, (unsigned)(p->reg & 0xff));
    if (mod == 0x16 || mod == 0x17)
      return 0;
    if (!eval_const(p->val, sec, pc, &off)) {
      symbol *base;
      /* only an indexed base address can be a symbol another file
       * defines; a based offset is always a small constant */
      if (mod == 0x0a && find_base(p->val, &base, sec, pc) == BASE_OK) {
        add_extnreloc(relocs, base, off, REL_ABS, 0, 16,
                       od->byteOffset + 1 + (int)extra_off);
        return 0;
      }
      cpu_error(0);
      return 1;
    }
    if (mod == 0x06) {
      if (off < 0 || off > 0xff) { cpu_error(10, (unsigned)off); return 1; }
      outbytes[od->byteOffset + 1] = (byte)off;
    } else
      write_word_le(outbytes, od->byteOffset + 1, (uint16)off);
    return 0;
  }

  default:
    break;
  }

  /* everything else carries an expression in p->val */

  /* OT_ADDR16R gets a first shot via local_label_disp() (above): plain
   * eval_const() below never succeeds for a same-file branch to a local
   * label in an ordinary relocatable section (confirmed empirically --
   * see local_label_disp()'s own comment), which would otherwise defer
   * EVERY such branch to a link-time relocation -- harmless for one
   * that's actually in range (vlink's S+A-P lands on the same value),
   * but it also means this backend could never tell, during its own
   * assembly, whether a branch needed relaxation. This resolves the
   * same-file case directly instead, the same way br_row_for_pass()'s
   * BR relaxation and cbranch_needs_relax()'s conditional-branch
   * relaxation (above) already need to for exactly this reason. A
   * cross-file or otherwise-unresolvable target falls through to the
   * ordinary relocation path below, unchanged. */
  if (od->opType == OT_ADDR16R) {
    taddr disp;
    if (local_label_disp(p, sec, pc, total, &disp)) {
      if (disp < -128 || disp > 127) { cpu_error(4); return 1; }
      write_masked(outbytes, od, (unsigned)((byte)disp));
      return 0;
    }
  }

  if (!eval_const(p->val, sec, pc, &val)) {
    symbol *base;
    /* Not a resolvable constant within this file. Before giving up, check
     * whether it's at least "one symbol plus a constant" (find_base()) --
     * e.g. a call/branch to a routine defined in another, not-yet-linked
     * file. eval_const()'s failure still leaves `val` holding whatever
     * constant remainder the expression evaluator could salvage (e.g. the
     * "+4" in "external_symbol+4"), which is exactly the addend a
     * relocation needs. size/bitoffset/byteoffset describe the field
     * vlink will patch; for OT_ADDR16R the addend also folds in
     * (byteOffset-total) so vlink's generic "S+A-P" (P = the relocated
     * field's own address) lands on the same pc-relative point our own
     * disp formula below uses (target-(pc+total)), not the field's own
     * address -- confirmed against how vasm's own z80 backend does the
     * equivalent adjustment for its relative jumps. */
    if (relocatable_type(od->opType) &&
        find_base(p->val, &base, sec, pc) == BASE_OK) {
      taddr addend = val;
      int reltype = REL_ABS;
      size_t size = 16;
      if (od->opType == OT_ADDR16R) {
        /* REL_MOD_S matters here, not just documentation: vlink's
         * checkrange() (support.c) treats an un-flagged ("sign==0,
         * unknown") relocation as fitting EITHER the signed OR the
         * unsigned interpretation of the field, i.e. accepts any
         * computed value in [-128,255] for an 8-bit field. That's wrong
         * for a real signed PC-relative branch displacement, whose true
         * range is [-128,127] -- confirmed empirically: a deliberately
         * out-of-range +200 displacement (a forward branch vasm can't
         * resolve locally, common in this multi-file/custom-linker-
         * script build once a section isn't based until link time,
         * even for a same-file target -- vasm's own eval_const() above
         * already failed for exactly that reason) linked with NO error
         * and silently wrote 0xC8, which the CPU reads as -56, not the
         * intended +200 -- a real miscompile, not a hypothetical one.
         * REL_MOD_S makes vlink apply the correct signed-only range and
         * reject it instead. */
        reltype = REL_PC | REL_MOD_S;
        size = 8;
        addend += (taddr)od->byteOffset - total;
      }
      add_extnreloc(relocs, base, addend, reltype, 0, size,
                     od->byteOffset + (int)extra_off);
      return 0;  /* bytes stay 0 here; vlink patches them in at link time */
    }
    cpu_error(0);  /* illegal operand -- not resolvable to a constant */
    return 1;
  }

  switch (od->opType) {
  case OT_BYTE: case OT_NUM:
    write_masked(outbytes, od, (unsigned)val & 0xff);
    return 0;

  case OT_WORD:
    write_word_le(outbytes, od->byteOffset, (uint16)val);
    return 0;

  case OT_SADDR:
    if (val < 0xfe20 || val > 0xff1f) { cpu_error(5, (unsigned)val); return 1; }
    write_masked(outbytes, od, saddr_byte_from_addr((uint16)val));
    return 0;

  case OT_SADDRP:
    if (val < 0xfe20 || val > 0xff1e) { cpu_error(5, (unsigned)val); return 1; }
    write_masked(outbytes, od, saddr_byte_from_addr((uint16)val));
    return 0;

  case OT_SADDRPP:
    if (val < 0xfe20 || val > 0xff1e) { cpu_error(5, (unsigned)val); return 1; }
    write_masked(outbytes, od, saddr_byte_from_addr((uint16)val));
    return 0;

  case OT_SADDR_FE:
  case OT_SADDRP_FE:
    if (val < 0xfe20 || val > 0xff1f) { cpu_error(9, (unsigned)val); return 1; }
    write_masked(outbytes, od, saddr_fe_byte_from_addr((uint16)val));
    return 0;

  case OT_SFR: case OT_SFRP:
    /* "sfr[0x4e]" syntax gives us just the low byte (0x00-0xff); promote
     * it to the full FF00H-FFFFH address sfr_byte_from_addr() expects. A
     * full address written out directly (0xff00-0xffff) also still works. */
    if (val >= 0 && val < 0x100)
      val |= 0xff00;
    if (val < 0xff00 || val > 0xffff) { cpu_error(6, (unsigned)val); return 1; }
    write_masked(outbytes, od, sfr_byte_from_addr((uint16)val));
    return 0;

  case OT_ADDR16:
    write_word_le(outbytes, od->byteOffset, (uint16)val);
    return 0;

  case OT_ADDR16R:
  {
    /* Unreachable in practice: the OT_ADDR16R fast path above (using
     * local_label_disp(), which also tries eval_const() as its first
     * step) already returns before this switch for every case that
     * lands here -- either it succeeded (and we're gone already) or it
     * failed for the same reason the eval_const() call that gated this
     * switch also just failed, in which case we never made it past the
     * relocation branch above either. Kept as a real, correct fallback
     * (not deleted) purely so this switch stays exhaustive over every
     * OperandType relocatable_type() lists, rather than relying on the
     * fast path never changing. disassembler formula: target =
     * (uint16)(pc+total) + (signed char)disp, so disp = target-(pc+total),
     * must fit a signed byte. */
    taddr disp = val - (pc + total);
    if (disp < -128 || disp > 127) { cpu_error(4); return 1; }
    write_masked(outbytes, od, (unsigned)((byte)disp));
    return 0;
  }

  case OT_ADDR11:
  {
    /* target = 0800H + raw11; raw11's high 3 bits live in byte0's low 3
     * bits (OR'd onto the fixed opcode pattern already there), low 8
     * bits are byte1 outright */
    unsigned raw11;
    if (val < 0x0800 || val > 0x0fff) { cpu_error(7, (unsigned)val); return 1; }
    raw11 = (unsigned)(val - 0x0800);
    outbytes[0] = (byte)(outbytes[0] | ((raw11 >> 8) & 0x07));
    outbytes[1] = (byte)(raw11 & 0xff);
    return 0;
  }

  case OT_ADDR5:
    if (val < 0 || val > 0x1f) { cpu_error(8, (unsigned)val); return 1; }
    outbytes[od->byteOffset] = (byte)(outbytes[od->byteOffset] | (val & 0x1f));
    return 0;

  case OT_N_BIT:
  case OT_CY_BIT:
    /* Both are a plain 3-bit field (bit number 0-7) at od->byteOffset/
     * opMask/opShift -- write_masked() already does exactly this,
     * identically for both operand types (shared/Opcodes.h's OP_N_BIT
     * and OP_CY_BIT_2 rows all use mask=0b111, shift=0). See
     * parse_operand()'s OT_CY_BIT comment for why OT_CY_BIT's own
     * accepted syntax ("cy.N") differs from what the disassembler prints. */
    if (val < 0 || val > 7) { cpu_error(11, (unsigned)val); return 1; }
    write_masked(outbytes, od, (unsigned)val);
    return 0;

  default:
    cpu_error(3);  /* not yet implemented */
    return 1;
  }
}

static const Opcode *opcode_for(instruction *ip)
{
  return &opps[mnemonics[ip->code].ext.opcode_idx];
}

/* Scan the shared opcode table for another row of the same mnemonic whose
 * operand1 is of the given type. Used for BR's relative/absolute pair (see
 * br_fits_relative() below) instead of hand-transcribing either row's op
 * byte or size here -- stays table-driven even for this dynamic-sizing
 * case, same as everything else in this backend. opps[] is tiny (a few
 * hundred rows) and this only runs for BR, so a linear scan is fine. */
static const Opcode *find_opps_row(Mnemonic m, int op1type)
{
  size_t i;
  for (i = 0; i < OPPS_COUNT; i++)
    if (opps[i].mnemonic == m && opps[i].operand1.opType == op1type)
      return &opps[i];
  return NULL;
}

/* Whether `p`'s expression is resolvable to a same-file displacement
 * right now, and if so, what it is: `(target) - (pc+total)`.
 *
 * This exists because vasm's own eval_expr() (expr.c) only folds a BARE
 * symbol reference to a constant when it carries the ABSLABEL flag (its
 * SYM case) -- a plain LOCREF label in an ordinary relocatable section
 * (e.g. ".section foo,\"a\"" / bare ".text", as every C-compiled module
 * in this project uses -- NOT ".org"-based like stage1.s) is left
 * non-constant there on purpose, even though its own `->pc` field
 * already holds a perfectly good section-relative offset by the time
 * resolve_section() is iterating (expr.c's SUB case, "sym1 - sym2" in
 * the same section, uses exactly that field the same way -- but that
 * special case only fires for a literal subtraction expression tree,
 * and parse_addr_expr() (above) never builds one: it stores the bare
 * target symbol and this backend subtracts pc itself, later, in plain
 * C). Net effect, confirmed empirically rather than assumed (task #52/
 * #53's Phase 1 build is real evidence, not a hypothetical): plain
 * eval_const() NEVER succeeds for a same-file branch to a local label
 * in a relocatable section, backward or forward, in range or not --
 * every single one was silently falling through to a deferred link-time
 * relocation. That's merely inefficient (not wrong) for an in-range
 * branch, since vlink's own S+A-P math lands on the same value -- but
 * it's fatal for relaxation specifically, which has to know the true
 * distance during vasm's OWN assembly to decide whether to grow the
 * encoding at all.
 *
 * The fix stays local to this backend rather than touching expr.c's
 * general symbol resolution: find_base() (already used for the
 * cross-file relocation fallback in encode_operand() below) already
 * hands back exactly the LABSYM + leftover addend needed -- this just
 * also checks base->sec==sec and, when true, reads base->pc directly
 * instead of deferring to a relocation. A cross-file (EXTREF) or
 * genuinely-external target still returns 0 here, unchanged. */
static int local_label_disp(operand *p, section *sec, taddr pc,
                             taddr total, taddr *disp)
{
  taddr val;
  symbol *base;

  if (!p || !p->val)
    return 0;
  if (eval_const(p->val, sec, pc, &val)) {
    /* Genuinely already constant (e.g. an .equ'd literal address, or a
     * label in an ABSOLUTE/".org"-based section like stage1.s) -- same
     * math as always, just centralized here too. */
    *disp = val - (pc + total);
    return 1;
  }
  if (find_base(p->val, &base, sec, pc) == BASE_OK &&
      LOCREF(base) && base->sec == sec) {
    /* BUG FIX 2026-09-30: `val` here is NOT a bare addend the way this
     * code originally assumed (mirroring encode_operand()'s cross-file
     * relocation fallback, where a failed eval_const() on
     * "external_symbol+4" really does leave just the "+4" remainder in
     * val, since the external symbol's value is genuinely unknown).
     * For a SAME-SECTION local label, vasm's eval_expr() already
     * substitutes the label's own known pc while simplifying the
     * expression, so the failed eval_const() call just above leaves
     * `val` holding the FULLY resolved target address (base->pc, plus
     * any real "+N" source addend already folded in) -- not a bare
     * addend on top of base->pc. Adding base->pc again here double-
     * counted it, confirmed empirically (emu78k3 regression testing):
     * "halt: br halt" -- a plain self-branch with zero addend --
     * assembled to a branch landing 7-8 bytes past its own address
     * instead of back onto itself, and in every case the error equalled
     * exactly the target's own pc (2x too much), including when an
     * explicit "target+2" addend was also present (val=base->pc+2 there
     * too, not just +2). This also means br_fits_relative()'s own range
     * check (-128..127) -- used by BR's short/absolute relaxation and by
     * the conditional-branch invert-and-jump relaxation, both built on
     * this same disp -- was checking a doubly-wrong magnitude too, not
     * just encode_operand()'s final byte: a target actually within range
     * could have been wrongly relaxed away (wasting 3 bytes, but still
     * correct), or, for any target whose true disp and doubled-error
     * magnitude land on opposite sides of the +-128 threshold, wrongly
     * judged "fits" and left as the short form while actually encoding a
     * garbage displacement -- i.e. this same bug could silently miscompile
     * an in-range-looking branch, not just self/near-loops. */
    *disp = val - (pc + total);
    return 1;
  }
  return 0;
}

/* Whether BR's short, 2-byte PC-relative encoding still fits, given the
 * current best-known pc. This is the piece the one-time parse_operand()
 * range check (cpu.c's OT_ADDR16R branch) can't do: that check only ever
 * sees a real value for a backward reference or a bare literal address --
 * a forward label is genuinely unresolved at parse time, so it's accepted
 * optimistically there and never reconsidered again... unless something
 * re-checks it on every relaxation pass, which is what this function is
 * for. vasm's own resolve_section() (vasm.c) already re-invokes
 * instruction_size()/eval_instruction() every pass with an updated pc and
 * keeps passing as long as any atom's size changes -- exactly the general
 * multi-pass relaxation machinery this needed, just not hooked up before.
 * `total` is always BR's short-form byte count (2): the candidate size
 * being tested this pass, matching the pc-relative formula's own
 * definition of "the address right after this instruction". */
static int br_fits_relative(operand *p, section *sec, taddr pc, taddr total)
{
  taddr disp;
  symbol *base;
  if (!p || !p->val)
    return 1;  /* shouldn't happen; don't force growth on missing data */
  /* A symbol never defined anywhere in this file (EXTREF: IMPORT-type, or
   * WEAK) is for the linker to resolve from another, separately-assembled
   * file -- and unlike a same-file forward label, this status is already
   * final on the very first pass (the whole file is parsed, so every
   * locally-defined symbol already has its LABSYM entry, before resolve()
   * ever runs), so there's no risk of this disagreeing with a later pass
   * the way guessing off eval_const()'s success/failure would. vlink
   * doesn't relax branches across files, and there's no way to know the
   * real cross-file distance at assemble time, so always play it safe and
   * use BR's absolute encoding for a cross-file target rather than
   * gambling on relative and risking silent range overflow at link time. */
  /* ...but only once parsing is over. vasm also sizes every instruction
   * while it is still parsing, and at that point a forward label that
   * simply hasn't been read yet looks exactly like an external symbol.
   * Treating it as "never fits" then made every forward branch start out
   * relaxed, pushing its own target 3 (or 5) bytes further away. For a
   * true displacement of +125..+127 that relaxed layout is self-
   * consistent -- the target really is out of range once the branch has
   * grown -- so the resolver never shrank it back. Found by reassembling
   * disassembled code: "be fwd" with fwd 125 bytes ahead came out as
   * BNE+BR instead of the original 2-byte BE. */
  if (parse_finished &&
      find_base(p->val, &base, sec, pc) == BASE_OK && EXTREF(base))
    return 0;
  if (!local_label_disp(p, sec, pc, total, &disp))
    return 1;  /* still unresolved -- optimistic default, same philosophy
                * as parse_operand()'s own accept-and-recheck-later
                * handling of a not-yet-resolvable forward reference;
                * grows into the absolute form only once actually proven
                * to not fit, and can keep re-trying every later pass as
                * more of the file resolves. */
  return disp >= -128 && disp <= 127;
}

/* For every row except BR's ADDR16R one, the row chosen at parse time is
 * final and its size never changes again. BR is the one case with a real
 * hardware fallback (an absolute 3-byte form) for when the relative 2-byte
 * one won't reach -- and the only way to find out is a forward reference
 * resolving too far away well after parsing already committed to the
 * short form. So for BR specifically, size (and, in eval_instruction()
 * below, the actual encoding) is decided fresh every pass instead of
 * fixed at parse time; every other mnemonic is unaffected. */
static const Opcode *br_row_for_pass(instruction *ip, const Opcode *o,
                                      section *sec, taddr pc)
{
  const Opcode *abs;
  if (o->mnemonic != M_BR || o->operand1.opType != OT_ADDR16R)
    return o;
  if (br_fits_relative(ip->op[0], sec, pc, (taddr)o->bytes))
    return o;
  abs = find_opps_row(M_BR, OT_ADDR16);
  return abs ? abs : o;  /* if somehow missing, keep the short form and
                           * let encode_operand()'s own exact range check
                           * raise the usual cpu_error(4) */
}

/* forward decl: real definition (with its own "why bytes==0 means 3"
 * comment) sits below, right before instruction_size()/eval_instruction()
 * which are its other two callers -- cbranch_needs_relax() just also
 * needs it a little earlier in file order. */
static int opcode_bytes(const Opcode *o);

/* ---------------------------------------------------------------------
 * Long-range expansion for short (8-bit-relative-only) conditional
 * branches: BE/BNE/BL/BNL/BGT/BGE/BLT/BLE/BH/BNH/BT/BF. Unlike BR
 * (br_fits_relative()/br_row_for_pass() above), none of these has a
 * second, absolute-addressed opcode row in shared/Opcodes.h -- confirmed
 * by inspection, and consistent with the datasheet: this CPU's
 * conditional branches are hardware-limited to a single signed-byte
 * PC-relative displacement, full stop, no long form exists to fall back
 * to the way BR falls back to its 3-byte absolute encoding. So when one
 * of these is out of range, the only correct fix is the standard
 * technique for any architecture with short-only conditional branches:
 * invert the condition and branch over an unconditional long jump (BR,
 * which DOES have the absolute form) that carries the real target.
 * Concretely, "bne far" (2 bytes) becomes a 5-byte pair:
 *     be   <here>+5     ; inverted condition, skip the BR below
 *     br   far           ; absolute, 3 bytes
 * Surfaced by real Phase 1 firmware code (eeprom.c/panel.c/pll.c, task
 * #52/#53), not a synthetic concern -- real functions large enough to
 * need it (page-safe EEPROM writes, the panel queue-pop, a PLL bit-bang
 * loop) exist today, and only get more common as the firmware grows.
 * --------------------------------------------------------------------- */

/* The logical negation of a branch mnemonic this relaxation knows how to
 * invert (returned via *out, 1 on success), or 0 if `m` isn't one of the
 * 12 branches this covers. M_BR is deliberately excluded -- it already
 * has its own long form and relaxation above, and never reaches here. */
static int invert_branch_mnemonic(Mnemonic m, Mnemonic *out)
{
  switch (m) {
  case M_BE:  *out = M_BNE; return 1;
  case M_BNE: *out = M_BE;  return 1;
  case M_BLT: *out = M_BGE; return 1;
  case M_BGE: *out = M_BLT; return 1;
  case M_BLE: *out = M_BGT; return 1;
  case M_BGT: *out = M_BLE; return 1;
  case M_BL:  *out = M_BNL; return 1;
  case M_BNL: *out = M_BL;  return 1;
  case M_BH:  *out = M_BNH; return 1;
  case M_BNH: *out = M_BH;  return 1;
  case M_BT:  *out = M_BF;  return 1;
  case M_BF:  *out = M_BT;  return 1;
  default:    return 0;
  }
}

/* Which operand slot (0/1/2) of Opcode row `o` carries the OT_ADDR16R
 * branch target, or -1 if none does. BE/BNE/BL/BNL/BGT/BGE/BLT/BLE/BH/
 * BNH carry it in operand1 (their only real operand); BT/BF carry it in
 * operand2 (operand1 is the bit source: saddr/sfr/a/x/pswh/pswl). */
static int addr16r_operand_index(const Opcode *o)
{
  if (o->operand1.opType == OT_ADDR16R) return 0;
  if (o->operand2.opType == OT_ADDR16R) return 1;
  if (o->operand3.opType == OT_ADDR16R) return 2;
  return -1;
}

static const Operand *operand_at(const Opcode *o, int idx)
{
  switch (idx) {
  case 0: return &o->operand1;
  case 1: return &o->operand2;
  case 2: return &o->operand3;
  }
  return NULL;
}

/* Resolves everything the inverted-branch-over-absolute-BR expansion of
 * row `o` needs: the inverted row (same addressing shape, opposite
 * condition), the operand slot carrying `o`'s own branch target, the
 * absolute-BR row, and the operand slot carrying the target in the
 * INVERTED row (structurally identical to `o`'s for every real pair
 * this project has -- re-derived rather than assumed, since nothing
 * here depends on that being true). Returns 0 if `o` isn't a branch
 * this relaxation covers, or (defensively) if a row it expects to exist
 * in shared/Opcodes.h is somehow missing -- callers treat that exactly
 * like "doesn't need relaxing" and let the normal short-form encoding's
 * own range check raise the usual, honest error rather than risk
 * emitting something unverified. */
static int find_relax_rows(const Opcode *o, const Opcode **inv_out,
                            const Opcode **babs_out, int *idx_out,
                            int *inv_idx_out)
{
  int idx, inv_idx;
  Mnemonic invm;
  const Opcode *inv, *babs;

  idx = addr16r_operand_index(o);
  if (idx < 0) return 0;
  if (!invert_branch_mnemonic(o->mnemonic, &invm)) return 0;
  inv = find_opps_row(invm, o->operand1.opType);
  if (!inv) return 0;
  inv_idx = addr16r_operand_index(inv);
  if (inv_idx < 0) return 0;
  babs = find_opps_row(M_BR, OT_ADDR16);
  if (!babs) return 0;

  *inv_out = inv;
  *babs_out = babs;
  *idx_out = idx;
  *inv_idx_out = inv_idx;
  return 1;
}

/* Whether row `o` needs the inverted-branch-over-absolute-BR expansion:
 * it must be one of the 12 branches above, carry a real ADDR16R operand,
 * and that operand's target must not fit the short form's range. Reuses
 * br_fits_relative() unmodified -- the range question (and its
 * same-pass-optimistic / cross-file-pessimistic handling) is identical
 * regardless of which mnemonic is asking. */
static int cbranch_needs_relax(instruction *ip, const Opcode *o,
                                section *sec, taddr pc)
{
  const Opcode *inv, *babs;
  int idx, inv_idx;

  if (!find_relax_rows(o, &inv, &babs, &idx, &inv_idx))
    return 0;
  return !br_fits_relative(ip->op[idx], sec, pc, (taddr)opcode_bytes(o));
}

/* ---------------------------------------------------------------------
 * DBNZ (decrement-and-branch-if-not-zero) has the same hardware limit as
 * every conditional branch above -- shared/Opcodes.h gives it two rows
 * (register form, OT_SADDR form), both OT_ADDR16R-only, no absolute
 * fallback -- but unlike a flag-test branch, it can't simply be
 * inverted: there is no instruction for "branch if this decrement WOULD
 * leave zero" without also performing the decrement, so inverting the
 * condition would mean decrementing twice (wrong) or needs an
 * instruction that doesn't exist. The standard long-range technique for
 * a decrement-and-branch with no long form and no usable inverse is a
 * 3-piece trampoline instead of the 2-piece invert-and-skip above:
 *     dbnz reg,Ltrampoline   ; unchanged operand, but retargeted to a
 *                            ; NEARBY point (always in range) instead
 *                            ; of the real (possibly far) target
 *     br   Lcontinue         ; reg==0 (fell through): skip the
 *                            ; trampoline, continue normally
 *   Ltrampoline:
 *     br   far               ; reg!=0 (branch taken): absolute jump to
 *                            ; the real target
 *   Lcontinue:
 * Both BRs here carry a fixed, always-in-range constant displacement (2
 * and 1 respectively) that never depends on where the real target is --
 * same technique as the flag-branch expansion's constant "3" above.
 * Surfaced by the same real Phase 1 code as the flag-branch case
 * (pll.c's bit-bang loop, compiled via machine.c's own DBNZ loop
 * optimization -- a real, load-bearing codegen path, not dead code). */
static int dbnz_needs_relax(instruction *ip, const Opcode *o,
                             section *sec, taddr pc)
{
  if (o->mnemonic != M_DBNZ || o->operand2.opType != OT_ADDR16R)
    return 0;
  return !br_fits_relative(ip->op[1], sec, pc, (taddr)opcode_bytes(o));
}

/* shared/Opcodes.h uses bytes==0 as a placeholder for every OT_MEM row:
 * the size depends on the addressing mode actually written, so it is
 * worked out per instruction by mem_mode_bytes() below. opcode_bytes()
 * is for the rows whose size is fixed (its 3 is only a fallback for an
 * OT_MEM row seen without its operands). */
static int opcode_bytes(const Opcode *o)
{
  return o->bytes ? o->bytes : 3;
}

/* Size of an OT_MEM instruction, from the mode its operand was written
 * in: 16H/17H 2 bytes, 06H 3 (+ offset), 0AH 4 (+ address). */
static int mem_mode_bytes(instruction *ip, const Opcode *o)
{
  int k;
  if (o->bytes)
    return o->bytes;
  for (k = 0; k < 3; k++) {
    const Operand *od = operand_at(o, k);
    if (od->opType == OT_MEM && ip->op[k]) {
      int mod = ip->op[k]->reg >> 8;
      return mod == 0x0a ? 4 : mod == 0x06 ? 3 : 2;
    }
  }
  return 3;
}

size_t instruction_size(instruction *ip, section *sec, taddr pc)
{
  const Opcode *o = br_row_for_pass(ip, opcode_for(ip), sec, pc);
  int short_total = mem_mode_bytes(ip, o);

  if (cbranch_needs_relax(ip, o, sec, pc))
    return (size_t)(short_total + 3);  /* + the trailing absolute BR */
  if (dbnz_needs_relax(ip, o, sec, pc))
    return (size_t)(short_total + 5);  /* + near BR + absolute BR */
  return (size_t)short_total;
}

dblock *eval_instruction(instruction *ip, section *sec, taddr pc)
{
  const Opcode *o = br_row_for_pass(ip, opcode_for(ip), sec, pc);
  int short_total = mem_mode_bytes(ip, o);
  int relax = cbranch_needs_relax(ip, o, sec, pc);
  int relax_dbnz = !relax && dbnz_needs_relax(ip, o, sec, pc);
  int total = relax ? short_total + 3 : relax_dbnz ? short_total + 5 : short_total;
  dblock *db = new_dblock();
  byte outbytes[8];
  int i;

  db->size = total;
  db->data = mymalloc((size_t)total);
  memset(outbytes, 0, sizeof(outbytes));

  if (relax) {
    /* find_relax_rows() re-derives exactly what cbranch_needs_relax()
     * just confirmed exists for this same `o` -- a pure function of
     * `o` alone, so this repeat call is guaranteed to succeed too. */
    const Opcode *inv, *babs;
    int idx, inv_idx, k;

    find_relax_rows(o, &inv, &babs, &idx, &inv_idx);

    /* Inverted short branch, same addressing shape as the original
     * (any non-branch operand -- BT/BF's bit source/bit number --
     * carries over unchanged; encode_operand() no-ops on OT_NONE, so
     * this is safe for the plain relational branches too, which have
     * no such operand). */
    outbytes[0] = (byte)(outbytes[0] | inv->op);
    if (inv->subOpMask != 0)
      outbytes[1] = (byte)(outbytes[1] | inv->subOp);
    for (k = 0; k < 3; k++) {
      if (k == inv_idx)
        continue;  /* the branch target itself: fixed skip constant below */
      encode_operand(ip->op[k], operand_at(inv, k), outbytes,
                      (taddr)short_total, sec, pc, &db->relocs, 0);
    }
    /* The inverted branch's own displacement is never the real (out-of-
     * range) target -- it always just skips the 3-byte absolute BR that
     * immediately follows, landing right after it. That's a fixed,
     * always-in-range constant regardless of where the real target is. */
    write_masked(outbytes, operand_at(inv, inv_idx), 3);

    /* Absolute BR, carrying the real target, right after the short
     * inverted branch -- encode_operand()'s own OT_ADDR16/relocatable_
     * type() path already handles a same-file OR cross-file target
     * correctly here, same as every other absolute reference. */
    outbytes[short_total] = (byte)(outbytes[short_total] | babs->op);
    encode_operand(ip->op[idx], &babs->operand1, outbytes + short_total,
                    (taddr)3, sec, pc + short_total, &db->relocs,
                    (taddr)short_total);
  } else if (relax_dbnz) {
    /* dbnz_needs_relax()'s own rationale (above) covers the full shape;
     * both BR rows are guaranteed to exist (M_BR always has both an
     * OT_ADDR16R and an OT_ADDR16 row in shared/Opcodes.h -- already
     * relied on unconditionally by br_row_for_pass() above). */
    const Opcode *brel = find_opps_row(M_BR, OT_ADDR16R);
    const Opcode *babs = find_opps_row(M_BR, OT_ADDR16);

    /* DBNZ itself: operand1 (the count register/saddr) unchanged;
     * operand2 (its branch target) retargeted to a fixed, always-in-
     * range constant -- skip straight to the absolute BR trampoline
     * below (short_total+2, i.e. past the near BR), regardless of
     * where the real target is. */
    outbytes[0] = (byte)(outbytes[0] | o->op);
    if (o->subOpMask != 0)
      outbytes[1] = (byte)(outbytes[1] | o->subOp);
    encode_operand(ip->op[0], &o->operand1, outbytes, (taddr)short_total,
                    sec, pc, &db->relocs, 0);
    write_masked(outbytes, &o->operand2, 2);

    /* Near BR: the reg==0 (fell through) case -- skip the 3-byte
     * absolute BR that follows, landing right after it. Fixed constant
     * "3" (the absolute BR's own size, not the near BR's) -- same value
     * the conditional-branch relaxation above uses to skip the same
     * 3-byte absolute BR shape; a constant of "1" here was wrong (it
     * lands inside the absolute BR's own opcode byte instead of past
     * it), caught by disassembling a forced-out-of-range synthetic test
     * rather than assumed correct from the size arithmetic alone. */
    outbytes[short_total] = (byte)(outbytes[short_total] | brel->op);
    write_masked(outbytes + short_total, &brel->operand1, 3);

    /* Absolute BR: the reg!=0 (branch taken) case -- the real target. */
    outbytes[short_total + 2] = (byte)(outbytes[short_total + 2] | babs->op);
    encode_operand(ip->op[1], &babs->operand1, outbytes + short_total + 2,
                    (taddr)3, sec, pc + short_total + 2, &db->relocs,
                    (taddr)(short_total + 2));
  } else {
    outbytes[0] = (byte)(outbytes[0] | o->op);
    if (o->subOpMask != 0)
      outbytes[1] = (byte)(outbytes[1] | o->subOp);

    encode_operand(ip->op[0], &o->operand1, outbytes, (taddr)total, sec, pc, &db->relocs, 0);
    encode_operand(ip->op[1], &o->operand2, outbytes, (taddr)total, sec, pc, &db->relocs, 0);
    encode_operand(ip->op[2], &o->operand3, outbytes, (taddr)total, sec, pc, &db->relocs, 0);

    /* MOV WDM,#imm / MOV STBC,#imm ("09 4x [~imm] [imm]") write a byte
     * and its one's complement as a guard against corrupting these two
     * protected control registers (watchdog mode, standby control) with
     * a runaway or glitched write -- shared/Opcodes.h's generic
     * per-operand model has no row-level way to express "this fixed
     * byte depends on that other operand", so it's special-cased here
     * instead. Confirmed against every "mov wdm/stbc,#imm" instance in
     * the mask ROM (82/82, zero exceptions): outbytes[2] (the byte
     * right after the op/subOp pair, before the immediate at
     * outbytes[3]) is always ~imm. */
    if (o->mnemonic == M_MOV &&
        (o->operand1.opType == OT_WDM || o->operand1.opType == OT_STBC))
      outbytes[2] = (byte)~outbytes[3];
  }

  for (i = 0; i < total; i++)
    db->data[i] = outbytes[i];
  return db;
}

/* ================= data directives (dc.b/dc.w/...) ================= */

dblock *eval_data(operand *op, size_t bitsize, section *sec, taddr pc)
{
  dblock *db = new_dblock();
  taddr val;

  db->size = (bitsize + 7) / 8;
  db->data = mymalloc((size_t)db->size);

  if (bitsize != 8 && bitsize != 16) {
    cpu_error(2);  /* data size not supported */
    memset(db->data, 0, (size_t)db->size);
    return db;
  }
  if (!eval_const(op->val, sec, pc, &val)) {
    /* Not a compile-time constant -- almost always a reference to a
     * symbol/label (in this file, not yet resolved at this point in the
     * single pass, or genuinely external, e.g. from another object file).
     * This previously had no fallback at all: eval_data() would silently
     * zero the bytes and report "illegal operand" for something as
     * ordinary as ".word some_label", discovered when the CALLT
     * table-placement work needed exactly that (".word <function-label>"
     * to place a function's real linked address into a fixed table
     * slot). Mirrors encode_operand()'s own find_base()+add_extnreloc()
     * fallback (see its comment on the OT_ADDR16R/relocatable_type()
     * path above) -- and the same pattern vasm's own z80 backend uses in
     * its eval_data(), confirmed there as the correct generic-vasm-core
     * calling convention (add_extnreloc(&db->relocs, ...), byteOffset=0
     * since the value fills this dblock from its own start, not a
     * position within a larger instruction). */
    symbol *base;
    if (find_base(op->val, &base, sec, pc) == BASE_OK) {
      add_extnreloc(&db->relocs, base, val, REL_ABS, 0, bitsize, 0);
      val = 0;  /* vlink patches the real bytes in at link time */
    } else {
      cpu_error(0);
      val = 0;
    }
  }
  if (bitsize == 8) {
    db->data[0] = (uint8_t)val;
  } else {
    db->data[0] = (uint8_t)(val & 0xff);
    db->data[1] = (uint8_t)((val >> 8) & 0xff);
  }
  return db;
}

/* ================= misc required entry points ================= */

/* NEC writes a bit operand as the byte it lives in, a dot, and the bit
 * number: "set1 flags.3", "bt flags.3,label", "mov1 cy,flags.3",
 * "mov1 flags.3,cy", "and1 cy,/flags.3". This backend's opcode rows carry
 * the bit as an operand of its own ("set1 flags,3", "bt flags,label,3") or
 * attached to CY ("mov1 cy.3,flags", "andi1 cy.3,flags"), so the NEC form
 * is rewritten into that form here, before the operands are matched, and
 * everything after works as before. Both forms are accepted.
 *
 * Only the operand that carries the bit is looked at (the first, or the
 * second when the first is CY), and it is split at its last dot outside
 * brackets, parentheses and quotes; what follows is the bit number, a
 * number or any expression. A name containing a dot (a local label such as
 * ".loop" is fine, it has nothing before the dot) can't be the byte operand
 * in this form. The rewrite only happens when the operand count is the NEC
 * one, so text already in this backend's form is never touched. */
static int bit_split(const char *p, int len)
{
  int i, depth = 0, last = -1;
  char q = 0;
  for (i = 0; i < len; i++) {
    char c = p[i];
    if (q) {
      if (c == '\\' && i + 1 < len) i++;
      else if (c == q) q = 0;
      continue;
    }
    if (c == '"' || (c == '\'' && !(i > 0 && isalpha((unsigned char)p[i - 1]))))
      q = c;
    else if (c == '(' || c == '[' || c == '{') depth++;
    else if (c == ')' || c == ']' || c == '}') depth--;
    else if (c == '.' && depth == 0) last = i;
  }
  /* something before the dot and something after it */
  if (last <= 0 || last >= len - 1)
    return -1;
  for (i = 0; i < last && isspace((unsigned char)p[i]); i++)
    ;
  return i < last ? last : -1;
}

static int is_cy(const char *p, int len)
{
  while (len > 0 && isspace((unsigned char)*p)) { p++; len--; }
  while (len > 0 && isspace((unsigned char)p[len - 1])) len--;
  return len == 2 && tolower((unsigned char)p[0]) == 'c' && tolower((unsigned char)p[1]) == 'y';
}

void cpu_fixup_bit_syntax(char **inst, int *inst_len, char **op, int *op_len, int *op_cnt)
{
  /* NEC operand count for each bit mnemonic (non-CY forms) */
  static const struct { const char *name; int nec; } bitm[] = {
    {"set1",1}, {"clr1",1}, {"not1",1}, {"bt",2}, {"bf",2}, {"btclr",2},
    {"bfset",2}, {"mov1",2}, {"and1",2}, {"or1",2}, {"xor1",2}
  };
  static char bitbuf[256], cybuf[260], mnbuf[8], abuf[256];
  char mn[8];
  int i, k, cy, split, inv = 0;
  char *a;
  int alen;

  if (*inst_len <= 0 || *inst_len >= (int)sizeof(mn))
    return;
  for (i = 0; i < *inst_len; i++)
    mn[i] = (char)tolower((unsigned char)(*inst)[i]);
  mn[i] = '\0';
  for (k = 0; k < (int)(sizeof(bitm) / sizeof(bitm[0])); k++)
    if (strcmp(mn, bitm[k].name) == 0)
      break;
  if (k == (int)(sizeof(bitm) / sizeof(bitm[0])) || *op_cnt < 1)
    return;

  /* which operand carries the bit, and is the other one CY? */
  cy = -1;
  i = 0;
  if (*op_cnt == 2 && is_cy(op[0], op_len[0])) { cy = 0; i = 1; }
  else if (*op_cnt == 2 && is_cy(op[1], op_len[1])) cy = 1;
  if (cy < 0 && (*op_cnt != bitm[k].nec || mn[0] == 'm' || mn[0] == 'a' || mn[0] == 'o' || mn[0] == 'x'))
    return;   /* already in this backend's form (mov1/and1/or1/xor1 always pair with CY) */
  split = bit_split(op[i], op_len[i]);
  if (split < 0 || split - 0 >= (int)sizeof(bitbuf) || op_len[i] - split - 1 >= (int)sizeof(bitbuf))
    return;

  a = op[i];
  alen = split;
  while (alen > 0 && isspace((unsigned char)*a)) { a++; alen--; }
  if (alen > 0 && *a == '/') {
    /* "cy,/flags.3" exists only for AND1 and OR1, with CY as the
     * destination; anywhere else the text is left alone, so vasm reports
     * the operand as invalid instead of silently dropping the '/' */
    if (cy != 0 || (strcmp(mn, "and1") != 0 && strcmp(mn, "or1") != 0))
      return;
    inv = 1;
    a++; alen--;
  }
  memcpy(bitbuf, op[i] + split + 1, (size_t)(op_len[i] - split - 1));
  bitbuf[op_len[i] - split - 1] = '\0';
  /* the byte operand gets its own terminated copy: the expression parser
   * reads a name to its end, and the dot is part of names */
  if (alen >= (int)sizeof(abuf) || (alen == 2 && is_cy(a, alen)))
    return;   /* "cy.3" is already this backend's form */
  memcpy(abuf, a, (size_t)alen);
  abuf[alen] = '\0';
  a = abuf;

  if (cy >= 0) {
    /* "mov1 cy,flags.3" -> "mov1 cy.3,flags" */
    snprintf(cybuf, sizeof(cybuf), "cy.%s", bitbuf);
    op[cy] = cybuf;
    op_len[cy] = (int)strlen(cybuf);
    op[i] = a;
    op_len[i] = alen;
    if (inv && (strcmp(mn, "and1") == 0 || strcmp(mn, "or1") == 0)) {
      snprintf(mnbuf, sizeof(mnbuf), "%si1", mn[0] == 'a' ? "and" : "or");
      *inst = mnbuf;
      *inst_len = (int)strlen(mnbuf);
    }
    return;
  }
  if (inv || *op_cnt >= MAX_OPERANDS)
    return;
  /* "bt flags.3,label" -> "bt flags,label,3" */
  op[i] = a;
  op_len[i] = alen;
  op[*op_cnt] = bitbuf;
  op_len[*op_cnt] = (int)strlen(bitbuf);
  (*op_cnt)++;
}

int init_cpu(void)
{
  return 1;
}

int cpu_args(char *p)
{
  (void)p;
  return 0;  /* no cpu-specific command line options yet */
}

char *parse_cpu_special(char *s)
{
  return s;  /* no cpu-specific directives yet */
}
