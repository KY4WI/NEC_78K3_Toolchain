/* otflags.h - flags ORed onto a mnemonics[] operand_type value.
 *
 * Shared by cpu.c (which strips and acts on them in parse_operand()) and
 * the table generator gen_mnemonics.c (which adds them). The values sit
 * above every real OT_* value in shared/Operand.h, so they can never be
 * mistaken for one.
 *
 * NEC writes the bit operand of a bit instruction as part of the byte it
 * lives in: "set1 flags.3", "bt flags.3,label", "mov1 cy,flags.3",
 * "and1 cy,/flags.3". The operand that holds the byte (saddr, sfr[], a, x,
 * pswh, pswl) is the one that carries the flags:
 *
 *   OTF_BITSFX  the operand may end in ".bit"; the bit expression is kept
 *               in operand.bit and the rest is parsed as the plain type.
 *   OTF_SLASH   (AND1/OR1 only) the operand must start with '/', which
 *               selects the ANDI1/ORI1 opcode rows: "and1 cy,/flags.3".
 */
#ifndef OTFLAGS_H
#define OTFLAGS_H
#define OTF_BITSFX 0x100
#define OTF_SLASH  0x200
#define OTF_MASK   (OTF_BITSFX | OTF_SLASH)
#endif
