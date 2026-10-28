/*  vbcc backend for the NEC 78K/III (uPD78312A / uPD78310A) 8/16-bit
    microcontroller family.

    This machine.h is derived from vbcc's own machines/generic/ template
    (see doc/interface.texi), with the register set and basic data types
    replaced by the real 78K/III values. It is confirmed against:
      - shared/Mnemonic.h and shared/Opcodes.h (our own verified
        disassembler/assembler opcode tables -- ground truth for which
        instructions actually exist on the 78312A)
      - NEC Application Note U12118EJ4V0AN00 ("78K/III Series Software
        Basic"), which documents the register-pair naming convention
        and confirms the 78312A is the base member of this family
      - the project's already-verified vasm backend (cpus/78k3/cpu.h),
        which independently confirms BIGENDIAN 0 for this target

    Code generation is functional but not complete. See machine.c
    and BUGS.md for implementation details and known limitations.
*/

#include "dt.h"

/* ------------------------------------------------------------------ */
/* Register set.                                                      */
/*                                                                     */
/* The 78K/III has 8 general 8-bit registers (functional names, RSS=0 */
/* absolute mapping):                                                 */
/*     X  A  C  B  E  D  L  H       (R0..R7)                          */
/* which combine into 4 hardware register pairs:                      */
/*     AX = (A:X)   BC = (B:C)   DE = (D:E)   HL = (H:L)               */
/* (high half named first, matching NEC's own convention and mirroring */
/*  our vasm/disassembler's already-verified RP encoding in            */
/*  shared/Opcodes.h -- OP_RP1/OP_RP1P etc.)                          */
/*                                                                     */
/* There are 4 selectable register BANKS (RB0-RB3, chosen via the PSW */
/* RBS bits and the SEL instruction), each holding its own independent */
/* copy of the 8 registers above -- similar in spirit to the Z80's     */
/* alternate register set: only one bank is "live" at a time. For a   */
/* first working backend we expose only the single active bank to the */
/* register allocator (8 byte-registers + 4 pairs); using the other   */
/* banks for e.g. fast interrupt entry is a possible future            */
/* optimization, not required for correctness.                        */
/*                                                                     */
/* The stack pointer (SP) is a dedicated hardware register implicitly  */
/* used by PUSH/POP/CALL/RET/stack-relative addressing -- it is NOT   */
/* part of the general register file and is therefore not modeled as  */
/* one of vbcc's allocatable registers (matching how the z80 and hc12  */
/* backends treat their own hardware SPs).                             */
/* ------------------------------------------------------------------ */

/* byte registers, numbered 1..8 */
#define REG_X 1
#define REG_A 2
#define REG_C 3
#define REG_B 4
#define REG_E 5
#define REG_D 6
#define REG_L 7
#define REG_H 8

/* register pairs, numbered 9..12 */
#define REG_AX 9
#define REG_BC 10
#define REG_DE 11
#define REG_HL 12

#define MAXR 12

/*  Number of commandline-options the code-generator accepts.       */
#define MAXGF 4

/*  Machine-specific addressing modes: not implemented yet. Once we  */
/*  want to fold e.g. saddr/sfr short-address forms or [HL+byte]     */
/*  indirect-with-offset forms into single instructions, this struct  */
/*  gains fields the way doc/interface.texi's addressingmodes section  */
/*  describes.                                                        */
struct AddressingMode {
    int never_used;
};

/*  If this is set to zero vbcc will not generate ICs where the     */
/*  target operand is the same as the 2nd source operand.           */
#define USEQ2ASZ 1

/*  Smallest/largest integer type that can be added to a pointer.   */
#define MINADDI2P INT
#define MAXADDI2P INT

/*  78K/III is little-endian (confirmed independently by our vasm    */
/*  backend's cpu.h and by the datasheet's byte-ordering diagrams).  */
#define BIGENDIAN 0
#define LITTLEENDIAN 1

/*  Generate SUB/TEST/BEQ instead of COMPARE/BEQ for switches: no    */
/*  particular hardware advantage known yet, leave off for now.      */
#define SWITCHSUBS 0

/*  No inline memcpy expansion yet -- revisit once MOVM/block-move   */
/*  instruction support (if any) is confirmed against the datasheet. */
#define INLINEMEMCPY 0

/*  Register-parameter passing: deliberately OFF for the first        */
/*  working version. We do not yet have an authoritative calling      */
/*  convention (NEC's own 78K/III C compiler is discontinued/         */
/*  unobtainable, and neither application note reviewed so far        */
/*  documents a C ABI). Passing everything on the stack is slower     */
/*  but unambiguously correct, and is the safe baseline to get a      */
/*  real end-to-end C-to-ROM pipeline working before optimizing the   */
/*  calling convention.                                                */
#undef HAVE_REGPARMS

/*  ORDERED_PUSH: 1 = PUSH ICs are generated left-to-right (first     */
/*  argument first); 0 (default) = right-to-left (last argument      */
/*  first). Our PUSH implementation in machine.c has no real hardware */
/*  single-register push to work with, so it just writes each pushed  */
/*  argument to the next sequential [sp+pushed] slot, starting from   */
/*  pushed=0, in whatever order the PUSH ICs arrive. That only lays   */
/*  arguments out the way every callee's own parameter-offset         */
/*  convention expects (leftmost parameter closest to SP) when the    */
/*  ICs themselves arrive left-to-right -- so we need this set to 1,  */
/*  overriding vbcc's default of right-to-left.                       */
#define ORDERED_PUSH 1

/*  We do need register pairs (AX/BC/DE/HL) to hold 16-bit ints.     */
#define HAVE_REGPAIRS 1

/*  size_t: 78K/III pointers are 16-bit, so unsigned int is natural. */
#define HAVE_INT_SIZET 1

/*  target-specific __near/__far/__interrupt attributes -- needed    */
/*  for ISR entry points and (eventually) banked-ROM addressing.     */
#define HAVE_TARGET_ATTRIBUTES

#define EMIT_BUF_LEN 1024
#define EMIT_BUF_DEPTH 4

#define HAVE_TARGET_PEEPHOLE 0
#undef HAVE_TARGET_EFF_IC
#undef HAVE_EXT_TYPES
#undef HAVE_TGT_PRINTVAL
#undef HAVE_EXT_IC

/* multiplication/division by powers of two -> shifts: the 78312A has
   no signed multiply/divide in hardware (only MULU/MULUW/DIVUW/DIVUX,
   confirmed via shared/Mnemonic.h), so this optimization matters more
   here than on machines with a cheap signed multiply instruction. */
#define HAVE_POF2OPT 1
