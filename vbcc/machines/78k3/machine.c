/*  vbcc backend for the NEC 78K/III (uPD78312A) microcontroller.

    STATUS (read this before trusting any output from this backend):

    The register file, data-type sizes, and the small well-defined
    predicate functions (freturn/regok/dangerous_IC/must_convert/
    reg_pair/cost_savings) below are REAL: they reflect the actual
    78312A register set and type widths, cross-checked against our
    own verified shared/Mnemonic.h + shared/Opcodes.h tables and the
    NEC User's Manual.

    gen_code() now handles real scalar (char/short/int/pointer)
    arithmetic (ADD/SUB/AND/OR/XOR), COMPARE + the six conditional
    branches, ASSIGN/CONVERT/ADDRESS/MINUS, and the return-value/
    register-move ICs (GETRETURN/SETRETURN/MOVETOREG/MOVEFROMREG),
    using the now-implemented [SP+byte] based-addressing mode for
    locals/parameters (see the comment above real_offset()) and the
    AX-bounce-through-HL trick in function_top()/function_bottom() for
    frame allocation (78312A can only move a value into SP from AX).
    Every branch condition, the ALU register-source restriction that
    drove regok()'s CHAR limitation, and the memory<->register
    fixed-A restriction that drove load_byte()/store_byte()/
    load_word()/store_word()'s design were each confirmed against
    shared/Opcodes.h and/or the NEC User's Manual, not assumed.

    gen_code() also now handles CALL/PUSH (function calls). The
    78312A has no usable single-register hardware push (PUSH/POP only
    exist as a register-LIST-bitmask form or the fixed-PSW form, per
    shared/Opcodes.h), so this is a FIXED_SP-style convention of our
    own design (there is no NEC-authored 78K/III C compiler ABI to
    match): gen_code() pre-scans the whole IC list for the largest
    single CALL's pushedargsize (argsize), reserves that on top of
    localsize as a per-function argument-build area, and each PUSH IC
    just writes its value to the next sequential [sp+pushed] slot,
    resetting pushed to 0 after each CALL. Two real, test-caught bugs
    were fixed to make this correct, not just "assembles without
    errors":

      1. #define ORDERED_PUSH 1 in machine.h. vbcc's default (0) emits
         PUSH ICs right-to-left (last argument first); our sequential
         first-processed-goes-to-lowest-offset PUSH code only lays
         arguments out the way every callee's own parameter-offset
         convention expects (leftmost parameter closest to SP) when
         PUSH ICs instead arrive left-to-right. Caught by hand-tracing
         add(x,y)'s generated call site against add()'s own prologue
         offsets before this was set -- x and y landed swapped.

      2. regsa[i]=1 (and regscratch[i]=0 to match, per interface.texi
         on regsa[]) for every one of our registers in init_cg(),
         reserving all of them out of the pool the core register
         allocator draws from for register variables and cross-IC
         temporaries. Every memory-operand MOV on this CPU must go
         through register A, so load_byte()/store_byte()/load_word()/
         store_word() (and the PUSH code that calls them) all use
         A/X/HL as internal scratch for the duration of a single IC,
         unconditionally. Before this fix regsa[i] was 0, so the
         allocator was free to also park some OTHER IC's live value in
         one of these same registers across several ICs (e.g. leaving
         a just-returned GETRETURN value resident in A, expecting it
         to survive until consumed) -- our scratch usage while
         generating any intervening IC would then silently clobber it.
         Caught by hand-tracing add(x, classify(c)): classify()'s
         return value in A was being overwritten by the very next
         instruction (loading x for its own PUSH) before it could be
         used as add()'s second argument. With regsa[i]=1 the
         allocator always spills such values to a real memory
         temporary instead, matching how plain C local variables are
         already handled. Re-verified correct (by hand-tracing the
         generated offsets against callee prologues) for 2-argument,
         3-argument, and nested-call-as-argument cases.

    Still deliberately unimplemented (calls unsupported_ic() rather
    than guessing): pointer dereference (DREFOBJ; needs register-
    indirect [de]/[hl]/... addressing wired up), and MULT/DIV/MOD/
    shifts (needs sign-correction wrappers around the unsigned-only
    hardware multiply/divide, and the SHR/SHL family). Each is a
    self-contained follow-up unit of work.
*/

#include "supp.h"

static char FILE_[]=__FILE__;

/* Independent backend version; does not depend on upstream Makefile rules
   or the Git history of the extracted source tree. */
#define VBCC78K3_VERSION "0.1"
char cg_copyright[]="vbcc 78K/III (uPD78312A) code-generator "
                    VBCC78K3_VERSION " (c) 2026 Dave (KY4WI)";

/* ------------------------------------------------------------------ */
/* Command-line flags. We don't need any yet; MAXGF (machine.h) must  */
/* still be >=1.                                                      */
/* ------------------------------------------------------------------ */
int g_flags[MAXGF]={0,0,0,0};
char *g_flags_name[MAXGF]={0,0,0,0};
union ppi g_flags_val[MAXGF];

/* ------------------------------------------------------------------ */
/* Data-type tables (required public globals).                        */
/* ------------------------------------------------------------------ */
zmax align[MAX_TYPE+1];
zmax maxalign;
zmax char_bit;
zmax sizetab[MAX_TYPE+1];
zmax t_min[MAX_TYPE+1];
zumax t_max[MAX_TYPE+1];
zumax tu_max[MAX_TYPE+1];

/* Sizes/alignments in BYTES, indexed by supp.h's own scalar-type
   constants (CHAR=1, SHORT=2, INT=3, LONG=4, LLONG=5, FLOAT=6,
   DOUBLE=7, LDOUBLE=8, VOID=9, POINTER=10, ARRAY=11, STRUCT=12,
   UNION=13, ENUM=14) -- confirmed by tracing init_cg()'s own copy
   loop just below (`sizetab[i]=l2zm(msizetab[i])` for i straight from
   0..MAX_TYPE, with no permutation), NOT dtgen.c's separate internal
   TYPECNT enumeration (error/char/uchar/short/... with signed/
   unsigned variants listed out separately) that an earlier version of
   this comment incorrectly described. That mismatched assumption left
   POINTER (index 10) holding a value meant for a different slot in
   the OTHER ordering, silently sizing every C pointer at several
   bytes rather than the real hardware's 16 bits. Caught by an actual
   test (t7.c, the first real pointer-dereference test): a function
   with a pointer parameter followed by an int parameter showed an
   unexplained multi-byte gap between them instead of the expected 2,
   traced back to this table.

   int==short==16 bits (native register-pair width) -- ENUM matches
   int, same convention every C compiler uses absent a reason not to.
   long/llong/float/double/ldouble are structurally required by the C
   type system (so sizeof()/struct layout come out right for code that
   merely declares them) but are not expected to be used by this
   project (no `long` or floating-point support needed) -- so they get
   standard C sizes but no codegen (gen_code() still calls
   unsupported_ic() for any real arithmetic on them). Their alignment
   is capped at maxalign (2, set in init_cg()) since nothing on this
   hardware ever needs wider alignment. ARRAY/STRUCT/UNION are not
   fixed-size scalar types at all -- their real size comes from their
   members/elements, computed elsewhere in vbcc's frontend -- so, like
   VOID, they just get a harmless 0-size/1-align placeholder, matching
   the convention already used by vbcc's own bundled
   machines/minx/machine.c (whose equivalent table was the original,
   never-rechecked-for-our-target source of these placeholder-style
   values). */
static long malign[MAX_TYPE+1]  ={1,1,2,2,2,2,2,2,2,1,2,1,1,1,2};
static long msizetab[MAX_TYPE+1]={0,1,2,2,4,8,4,8,8,0,2,0,0,0,2};

/* ------------------------------------------------------------------ */
/* Register set (see machine.h for the numbering and reasoning).      */
/* ------------------------------------------------------------------ */
char *regnames[MAXR+1];
zmax regsize[MAXR+1];
struct Typ *regtype[MAXR+1];
int regsa[MAXR+1];
int regscratch[MAXR+1];
/* reg_prio[] and cost_savings() are provided by the core (regs.c)
   unless HAVE_TARGET_RALLOC is defined -- we don't define it, so we
   must not redeclare them here. */

/* __callt_slotN (N=0-31): NEC 78K/III CALLT instruction support. CALLT is
   a 1-byte call through a fixed 32-entry, 2-byte-per-entry address table
   at 0x0040-0x007F (CALLT's operand is a hardware 5-bit field -- OT_ADDR5
   in shared/Operand.h -- so exactly 32 slots, 64 bytes; NOT 0x0040-0x007E
   as the project's own firmware doc currently has it, off by one byte).
   The stock TK-x4x firmware uses this heavily (panel-byte send, EEPROM
   helpers, the BU4094/M62363 writes, UART send/receive -- see
   TK-x4x_HARDWARE_COMMUNICATION-9.md section 17.1), and a CALLT target
   doesn't have to be a function we compile: it can equally be an
   existing resident/mask-ROM routine (or a hand-written ABI shim to one)
   reached through a slot Kenwood's own code already populates -- exactly
   how the stock firmware itself uses entries like "CALLT -> 808F -> BR
   1F24". This backend's g_attr_name[]/tattr bitmask distinguishes the
   two cases for free, with no extra attribute needed:
     - __callt_slotN on a function DEFINED in this translation unit (a
       real C body compiled here): function_top() records it and
       cleanup_cg() emits the table entry pointing at its own address.
     - __callt_slotN on a function only DECLARED (prototype/extern, no
       body compiled here -- e.g. a resident routine, or a hand-written
       __asm() shim to one): the CALL IC still emits "callt [N]" at every
       call site (see the CALL case below), but cleanup_cg() never
       touches that slot, since function_top() -- the only place a table
       entry gets recorded -- only ever runs for a real definition. */
char *g_attr_name[]={
  "__interrupt",
  "__callt_slot0","__callt_slot1","__callt_slot2","__callt_slot3",
  "__callt_slot4","__callt_slot5","__callt_slot6","__callt_slot7",
  "__callt_slot8","__callt_slot9","__callt_slot10","__callt_slot11",
  "__callt_slot12","__callt_slot13","__callt_slot14","__callt_slot15",
  "__callt_slot16","__callt_slot17","__callt_slot18","__callt_slot19",
  "__callt_slot20","__callt_slot21","__callt_slot22","__callt_slot23",
  "__callt_slot24","__callt_slot25","__callt_slot26","__callt_slot27",
  "__callt_slot28","__callt_slot29","__callt_slot30","__callt_slot31",
  "__callt",
  0
};
/* declaration.c's HAVE_TARGET_ATTRIBUTES path sets tattr|=(1L<<i) where i
   is the g_attr_name[] index of the recognized attribute name -- index 0
   is "__interrupt", so bit 0 (value 1) of v->tattr means the function was
   declared __interrupt. Same convention as machines/c16x/machine.c's own
   INTERRUPT bit, which this follows as a reference (though not its
   regused[]-driven partial-save prologue -- see function_top()'s comment
   for why that doesn't apply to this backend). Indices 1-32 are the 32
   __callt_slotN names, giving tattr bits 1-32 (fits comfortably in the
   64-bit "unsigned long" tattr on this host -- see supp.h). vbcc's
   attribute parser (declaration.c) only matches bare keyword names, with
   no support for a parenthesized argument like "callt_slot(19)" -- one
   bare name per slot reuses that exactly as-is, with zero changes to
   shared core code. Callers get ergonomic syntax anyway via a
   preprocessor macro (see the 78k3 target header) that token-pastes the
   slot number onto the attribute name before the compiler's attribute
   parser ever sees it.

   Index 33 is a 34th, deliberately NUMBERLESS name, "__callt": "put this
   in CALLT somewhere, I don't care which slot" -- see callt_resolve()/
   callt_auto_assign() below for how a real number gets picked. Only ever
   meaningful on a function actually DEFINED in this file (the whole
   point of a numberless request is "pick me an address slot," and there
   is no address without a body) -- using it on a declare-only prototype
   for an existing resident routine is a user error, since picking a
   number for that case would be meaningless: that routine's real slot is
   whatever RESET's boot code (or a hand-written table entry) already
   wrote, a hardware/firmware fact to discover, not a free choice. That
   specific misuse isn't separately diagnosed (see callt_auto_assign()'s
   comment for why: the ordinary undefined-symbol link error it falls
   into either way is an acceptable, unsurprising failure mode, not a
   silent one -- not worth extra bookkeeping to turn into a nicer
   message). Deliberately NOT building the further step discussed and
   deferred 2026-10-11: auto-filling slots nobody asked for, by picking
   whichever functions have the most call sites. That needs whole-
   program call counts before any number gets handed out (either a
   two-pass build or a vlink-side CALL-to-CALLT relaxation pass), and
   there's no real compiled firmware yet to even measure call counts
   against -- revisit once there is. Hand-pick known-hot resident/helper
   functions with an explicit __callt_slotN in the meantime; this bare
   form only covers "I don't care which number," not "pick my busiest
   functions for me." */
#define INTERRUPT 1
#define CALLT_SLOT_BASE_BIT 1
#define CALLT_SLOT_COUNT 32
#define CALLT_SLOT_BIT(n) (1UL<<(CALLT_SLOT_BASE_BIT+(n)))
#define CALLT_ANY_SLOT_MASK (((1UL<<CALLT_SLOT_COUNT)-1UL)<<CALLT_SLOT_BASE_BIT)
#define CALLT_AUTO_BIT (1UL<<(CALLT_SLOT_BASE_BIT+CALLT_SLOT_COUNT))

/* -1 if v has no __callt_slotN attribute, else the slot number (0-31).
   Diagnoses (fatal) a function carrying more than one __callt_slotN
   attribute at once -- declaration.c's own g_attr_name loop only catches
   the SAME attribute repeated (its error 227), not two DIFFERENT slot
   numbers together, since each is a distinct bit as far as core cares. */
static int callt_slot_of(struct Var *v)
{
  unsigned long bits=v->tattr&CALLT_ANY_SLOT_MASK;
  int n,found=-1;
  if(!bits) return -1;
  for(n=0;n<CALLT_SLOT_COUNT;n++){
    if(bits&CALLT_SLOT_BIT(n)){
      if(found>=0){
        fprintf(stderr,
          "vbcc78k3: error: function \"%s\" has more than one "
          "__callt_slotN attribute (slot %d and slot %d)\n",
          v->identifier?v->identifier:"?",found,n);
        raus();
      }
      found=n;
    }
  }
  return found;
}

/* Table-entry bookkeeping for functions actually DEFINED with a
   __callt_slotN attribute in this translation unit (see the big comment
   above g_attr_name[]) -- populated by function_top(), consumed once by
   cleanup_cg() at the very end of the file, so each slot's section is
   emitted in one place instead of interleaved with function bodies. NULL
   means that slot isn't claimed by a definition in this TU (either
   unused, or __callt_slotN-attributed but only declared -- a resident-
   routine call site, not something we place into the table). See
   cleanup_cg()'s own comment for why this is one section per slot
   (calltblN), not one dense block -- that's what makes cross-file
   collision detection the LINKER's job instead of something this single
   compiler invocation could ever fully check on its own. */
static char *callt_slot_owner[CALLT_SLOT_COUNT];   /* C identifier, for diagnostics */
static char *callt_slot_symbol[CALLT_SLOT_COUNT];  /* full asm label text, for the .word entry */

/* Called from function_top() (an explicit __callt_slotN) or
   callt_auto_assign() (a bare __callt) when defining a function that
   claims a table slot. Diagnoses two DIFFERENT functions in this TU
   claiming the same slot -- task #40's collision check. (Cross-
   translation-unit collisions aren't caught HERE -- a single compiler
   invocation only ever sees one file's own claims. Real multi-file
   linking is no longer the gap this comment originally described,
   though: every firmware image's build.sh already compiles several .c
   files and links the results with vlink -- see e.g.
   firmware/baudsweep/build.sh. What's actually true today is narrower:
   no firmware image currently uses __callt_slotN/__callt at all, so
   cross-file CALLT collisions specifically haven't been exercised by a
   real build yet. calltbl.ld's one-MEMORY-region-per-slot design exists
   precisely so that whenever that day comes, vlink turns a cross-file
   collision into a hard "no space to fit section" link error instead of
   silently overlapping two files' table entries -- see calltbl.ld's own
   comment for the empirical confirmation of that.) */
static void callt_slot_claim(int slot,const char *name,const char *symbol)
{
  if(callt_slot_owner[slot]){
    if(strcmp(callt_slot_owner[slot],name)!=0){
      fprintf(stderr,
        "vbcc78k3: error: __callt_slot%d claimed by both \"%s\" and "
        "\"%s\" in this file\n",slot,callt_slot_owner[slot],name);
      raus();
    }
    return;  /* same function redefined/reseen -- not a real collision */
  }
  callt_slot_owner[slot]=mystrdup((char *)name);
  callt_slot_symbol[slot]=mystrdup((char *)symbol);
}

/* Hands an auto-requesting (bare __callt) identifier a real slot number,
   the first time anyone asks for it -- either a call site reached before
   the callee's own definition (this is a single-pass-per-function
   compiler: nothing guarantees a forward-called function's function_top()
   has already run) or the definition itself, whichever comes first in
   file order. Reuses callt_slot_claim() for the actual bookkeeping, so a
   second ask for the SAME name gets the SAME slot back for free (the
   first loop below finds it before the second loop ever looks at a free
   one), and a genuine clash still can't happen here since a slot this
   function hands out always starts from callt_slot_owner[n]==NULL.

   Known, accepted limitation: if an identifier is auto-requested (at a
   call site, say) but never actually given a body anywhere in this file
   -- a typo, or __callt used by mistake on what was meant to be a
   declare-only resident-routine reference -- this still spends a slot
   and still emits a ".word <symbol>" for it in cleanup_cg(), pointing at
   whatever that symbol resolves to (nothing, in the whole program, most
   likely). That surfaces as an ordinary undefined-symbol error from the
   assembler/linker, same as calling any other never-defined function
   normally would -- not a new or silent failure mode, just a less
   specific error message than a dedicated check could give. Not worth
   extra bookkeeping to improve on that right now. */
static int callt_auto_assign(const char *name,const char *symbol)
{
  int n;
  for(n=0;n<CALLT_SLOT_COUNT;n++){
    if(callt_slot_owner[n]&&strcmp(callt_slot_owner[n],name)==0) return n;
  }
  for(n=0;n<CALLT_SLOT_COUNT;n++){
    if(!callt_slot_owner[n]){
      callt_slot_claim(n,name,symbol);
      return n;
    }
  }
  fprintf(stderr,
    "vbcc78k3: error: no free CALLT slot left to auto-assign \"%s\" "
    "(all 32 slots already claimed)\n",name);
  raus();
  return -1; /* unreached */
}

/* Forward declaration: callt_symbol_text()'s own definition sits next to
   function_top(), far below (it's a wrapper for a label-emission detail
   function_top() implements), but callt_resolve() -- used by both
   function_top() AND the CALL IC case, so it has to live up here, above
   both -- needs to call it too, for the auto-assign path. */
static char *callt_symbol_text(struct Var *v);

/* The single lookup both function_top() and the CALL IC case go through:
   resolves v's CALLT slot (0-31), or -1 if v isn't CALLT-bound at all.
   Keeps the two attribute forms mutually exclusive -- there's no
   sensible way to honor an explicit number and "pick for me" at once --
   and otherwise just dispatches to whichever one v actually has. The
   explicit-slot case returns the number ONLY; it deliberately does not
   call callt_slot_claim() itself, exactly as before this function
   existed -- that claim still happens solely in function_top(), i.e.
   solely for a real definition, never for a declare-only resident-
   routine reference. */
static int callt_resolve(struct Var *v)
{
  int explicit_slot=callt_slot_of(v);
  int wants_auto=(v->tattr&CALLT_AUTO_BIT)!=0;
  if(explicit_slot>=0&&wants_auto){
    fprintf(stderr,
      "vbcc78k3: error: function \"%s\" has both an explicit __callt_slot%d "
      "and the auto-assign __callt attribute -- use one or the other\n",
      v->identifier?v->identifier:"?",explicit_slot);
    raus();
  }
  if(explicit_slot>=0) return explicit_slot;
  if(wants_auto) return callt_auto_assign(v->identifier?v->identifier:"?",
                                           callt_symbol_text(v));
  return -1;
}

static struct Typ ityp={INT},chtyp={CHAR};

/* sections */
#define DATA 0
#define BSS 1
#define CODE 2
#define RODATA 3
static int section=-1,newobj;
static char *textname="\t.text\n",*dataname="\t.data\n",*bssname="\t.bss\n";
static char *labprefix="l",*idprefix="_";

static void emit_obj(FILE *f,struct obj *p,int t);

/* ------------------------------------------------------------------ */
/* init_cg()                                                           */
/* ------------------------------------------------------------------ */
int init_cg(void)
{
  int i;

  maxalign=l2zm(2L);
  char_bit=l2zm(8L);
  stackalign=l2zm(2L);

  for(i=0;i<=MAX_TYPE;i++){
    sizetab[i]=l2zm(msizetab[i]);
    align[i]=l2zm(malign[i]);
  }

  t_min[CHAR]=l2zm(-128L);
  t_min[SHORT]=l2zm(-32768L);
  t_min[INT]=l2zm(-32768L);
  t_min[LONG]=zmsub(l2zm(-2147483647L),l2zm(1L));
  t_min[LLONG]=zmlshift(l2zm(1L),l2zm(63L));
  t_min[MAXINT]=t_min[LLONG];
  t_max[CHAR]=ul2zum(127UL);
  t_max[SHORT]=ul2zum(32767UL);
  t_max[INT]=ul2zum(32767UL);
  t_max[LONG]=ul2zum(2147483647UL);
  t_max[LLONG]=zumrshift(zumkompl(ul2zum(0UL)),ul2zum(1UL));
  t_max[MAXINT]=t_max[LLONG];
  tu_max[CHAR]=ul2zum(255UL);
  tu_max[SHORT]=ul2zum(65535UL);
  tu_max[INT]=ul2zum(65535UL);
  tu_max[LONG]=ul2zum(4294967295UL);
  tu_max[LLONG]=zumkompl(ul2zum(0UL));
  tu_max[MAXINT]=tu_max[LLONG];

  regnames[0]="noreg";
  regnames[REG_X]="x";  regnames[REG_A]="a";  regnames[REG_C]="c";
  regnames[REG_B]="b";  regnames[REG_E]="e";  regnames[REG_D]="d";
  regnames[REG_L]="l";  regnames[REG_H]="h";
  regnames[REG_AX]="ax"; regnames[REG_BC]="bc";
  regnames[REG_DE]="de"; regnames[REG_HL]="hl";

  for(i=REG_X;i<=REG_H;i++){
    regsize[i]=l2zm(1L);
    regtype[i]=&chtyp;
    /* regsa[i]=1 reserves every one of our registers out of the pool
       the core register allocator draws from for register variables
       and cross-IC temporaries (interface.texi on regsa[]: "The
       compiler will not use any of those registers for register
       variables or temporaries ... reasonable if for many ICs code
       cannot be generated without using additional registers" --
       exactly our situation: every memory-operand MOV on this CPU
       must go through register A, so load_byte()/store_byte()/
       load_word()/store_word() etc. all use A/X/HL as internal
       scratch, unconditionally, for the DURATION OF A SINGLE IC
       only. If the allocator were free to also hand out these same
       registers to hold some OTHER IC's live value across several
       ICs (e.g. parking a GETRETURN result in A, expecting it to
       still be there several ICs later), our scratch usage while
       generating any IC in between would silently clobber it -- this
       is exactly the bug a real test (t5.c's mixed_call(), calling
       add(x, classify(c))) caught: classify()'s return, left
       resident in A across the following PUSH-of-x code (which
       legitimately uses A as pure scratch to shuffle x's bytes into
       the argument-build area), was overwritten before it could be
       used as add()'s second argument. Marking every register
       regsa[i]=1 forces the allocator to always spill such values to
       a real memory temporary instead of leaving them parked in one
       of these registers between ICs, matching how our backend
       already treats plain C local variables. Per the docs, any
       register with regsa[i]=1 must have regscratch[i]=0. */
    regsa[i]=1;
    regscratch[i]=0;
    reg_prio[i]=1;
  }
  for(i=REG_AX;i<=REG_HL;i++){
    regsize[i]=l2zm(2L);
    regtype[i]=&ityp;
    regsa[i]=1;
    regscratch[i]=0;
    reg_prio[i]=1;
  }

  multiple_ccs=0;
  target_macros=0;

  return 1;
}

void init_db(FILE *f)
{
}
void cleanup_db(FILE *f)
{
}
/* Emits one ".section calltblN" per __callt_slotN function actually
   DEFINED in this file (see the big comment above g_attr_name[] for why
   declarations-only don't appear here) -- each holding exactly its own
   single ".word" entry, nothing else. A file that doesn't claim slot N
   emits no calltblN section at all: no dense table, no ".word 0"
   gap-filling here.

   This used to build one dense, zero-filled block covering slot 0
   through this file's own highest used slot -- which only worked by
   accident for a single file. Two files linked together would each
   contribute their own same-named "calltbl" section, and vlink merges
   same-named input sections from different files by concatenating them
   at the section's declared address -- confirmed empirically: two 2-byte
   contributions silently became a 4-byte run at the fixed address,
   overflowing into the NEXT slot with exit code 0, no warning at all.
   Splitting into one section per slot doesn't fix that by itself --
   plain `calltblN <addr> : { *(calltblN) }` still merges two files'
   colliding contributions the same silent way -- so the real fix is on
   the linker-script side, not here: give each calltblN its own MEMORY
   region sized to exactly 2 bytes. Confirmed empirically that this turns
   a genuine two-file collision into a hard link error ("No space to fit
   section ... into memory region"), correctly names the offending file,
   and still resolves a truly unclaimed slot to a silent zero (no
   region contribution at all -- rawbin1 zero-fills the gap, same as any
   other unplaced address range). See calltbl.ld (generated once,
   checked in -- the slot addresses are a fixed hardware fact, nothing
   to regenerate per build) for the actual MEMORY+SECTIONS script; this
   function only emits the per-slot sections; placement is entirely the
   linker's job, matching how a real multi-file link needs to detect
   this (a single compiler invocation can never see another file's slot
   claims -- only the linker sees every object file at once). */
void cleanup_cg(FILE *f)
{
  int n;
  for(n=0;n<CALLT_SLOT_COUNT;n++){
    if(callt_slot_symbol[n]){
      emit(f,"\t.section\tcalltbl%d,\"a\"\n",n);
      emit(f,"\t.word\t%s\n",callt_slot_symbol[n]);
    }
  }
}

/* ------------------------------------------------------------------ */
/* Register pairs: AX=(A:X), BC=(B:C), DE=(D:E), HL=(H:L).             */
/* Naming/ordering matches shared/Opcodes.h's already-verified RP      */
/* encoding and the register table in NEC AN U12118EJ4V0AN00.          */
/* ------------------------------------------------------------------ */
int reg_pair(int r,struct rpair *p)
{
  switch(r){
  case REG_AX: p->r1=REG_A; p->r2=REG_X; return 1;
  case REG_BC: p->r1=REG_B; p->r2=REG_C; return 1;
  case REG_DE: p->r1=REG_D; p->r2=REG_E; return 1;
  case REG_HL: p->r1=REG_H; p->r2=REG_L; return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* freturn(): values that fit in a register pair (<=2 bytes) come back */
/* in A (1-byte types) or AX (2-byte types). Anything bigger (long,     */
/* struct/union) is returned via a hidden pointer, which vbcc's        */
/* frontend arranges for automatically when freturn() returns 0 --     */
/* this is a safe, standard, always-correct choice and does not depend */
/* on knowing NEC's own (unobtainable) compiler's ABI, since we are    */
/* not required to match it.                                          */
/* Deliberately REG_A rather than REG_AX for 1-byte types: gen_code()'s */
/* GETRETURN/SETRETURN/MOVETOREG/MOVEFROMREG all dispatch byte-vs-word */
/* handling on regtype[reg]->flags==CHAR, which is only ever true for  */
/* REG_A (see init_cg()) -- returning REG_AX for a char here would     */
/* make that dispatch treat a 1-byte value as a 2-byte one and read/   */
/* write a stray extra byte. (Caught by an actual build+run test: an   */
/* earlier version of this function did exactly that.)                */
/* ------------------------------------------------------------------ */
int freturn(struct Typ *t)
{
  if(ISFLOAT(t->flags)) return 0;
  if(ISSTRUCT(t->flags)||ISUNION(t->flags)) return 0;
  if(zmleq(szof(t),l2zm(1L))) return REG_A;
  if(zmleq(szof(t),l2zm(2L))) return REG_AX;
  return 0;
}

int regok(int r,int t,int mode)
{
  if(r==0) return 0;
  t&=NQ;
  if(t==0) return 0; /* no condition-code registers in this backend yet */
  if(t==CHAR||t==(CHAR))
    /* NOT r>=REG_X&&r<=REG_H: shared/RegisterNames.h's REGNAMES_R1 table
       (the "r1" register-source field used by every 8-bit ALU op's
       register-register row, e.g. "add r,r1", and by the fixed-A forms
       "mov a,r1"/"xch a,r1") only contains x/a/c/b (+ the alternate-bank
       primed forms we never use) -- d/e/h/l are NOT in it. So a char
       value sitting in d/e/h/l could never be used as an ALU or MOV
       source in the generic register-register encodings, only ever
       moved further away (R1 is never a valid destination for those
       registers either, except by first bouncing through a register-
       PAIR move -- see load_word()/store_word() below). Simplest correct
       fix: never let a scalar char live independently in d/e/h/l at all;
       they stay reserved as halves of the DE/HL pairs. Confirmed against
       shared/Opcodes.h's actual OP_R1-using rows, not assumed. */
    return r==REG_X||r==REG_A||r==REG_C||r==REG_B;
  if(t==SHORT||t==INT||t==POINTER)
    return r>=REG_AX&&r<=REG_HL;
  /* long/llong/float/double do not fit any single register of ours;
     vbcc will keep them in memory. */
  return 0;
}

int dangerous_IC(struct IC *p)
{
  int c=p->code;
  if((p->q1.flags&DREFOBJ)||(p->q2.flags&DREFOBJ)||(p->z.flags&DREFOBJ))
    return 1;
  if((c==DIV||c==MOD)&&!((p->q2.flags&(KONST|DREFOBJ))==KONST))
    return 1;
  return 0;
}

int must_convert(int o,int t,int const_expr)
{
  int op=o&NQ,tp=t&NQ;
  /* int and pointer are both 16-bit and identically represented */
  if((op==INT||op==POINTER)&&(tp==INT||tp==POINTER)) return 0;
  return 1;
}

int shortcut(int code,int t)
{
  /* char/short ops map directly onto our real byte/word registers,
     so there's no promotion penalty to avoid. */
  return 1;
}

/* ------------------------------------------------------------------ */
/* Assembly emission helpers.                                         */
/* ------------------------------------------------------------------ */

static void unsupported_ic(struct IC *p)
{
  fprintf(stderr,"78k3 backend: IC code %d not yet implemented "
                 "(this backend only handles trivial functions so far)\n",
                 p->code);
  ierror(0);
}

/* VARADR: the operand is the ADDRESS of a static or external object,
   not its contents. vbcc's optimizer turns "ADDRESS of a static" into an
   ASSIGN with q1.flags|=VARADR (opt.c), and string literals and static
   arrays reach calls and pointer arithmetic this way. Before this was
   handled, load_word() read the object's first two bytes as if they were
   the pointer: panel_display_text("PANEL OK") was passed 4150h ("PA"),
   and on the radio's multiplexed bus a read of that unmapped address
   returns its own low byte, so the display showed "PQRSTUVW". */
static void emit_varadr(FILE *f,struct obj *o)
{
  long off=zm2l(o->val.vmax);
  if(o->v->storage_class==STATIC)
    emit(f,"%s%ld",labprefix,zm2l(o->v->offset));
  else
    emit(f,"%s%s",idprefix,o->v->identifier);
  if(off) emit(f,"+%ld",off);
}

/* emit_obj() for callers that supply their own prefix (or none, as in
   initializer data): VARADR is dropped so the label is printed bare. */
static void emit_obj_bare(FILE *f,struct obj *p,int t)
{
  int va=p->flags&VARADR;
  p->flags&=~VARADR;
  emit_obj(f,p,t);
  p->flags|=va;
}

static void emit_obj(FILE *f,struct obj *p,int t)
{
  if((p->flags&(KONST|DREFOBJ))==(KONST|DREFOBJ)){
    emitval(f,&p->val,p->dtyp&NU);
    return;
  }
  if(p->flags&REG){
    emit(f,"%s",regnames[p->reg]);
    return;
  }
  if(p->flags&KONST){
    emit(f,"#");
    emitval(f,&p->val,t&NU);
    return;
  }
  if((p->flags&(VAR|VARADR|DREFOBJ))==(VAR|VARADR)){
    emit(f,"#");
    emit_varadr(f,p);
    return;
  }
  if(p->flags&VAR){
    if(p->v->storage_class==STATIC)
      emit(f,"%s%ld",labprefix,zm2l(p->v->offset));
    else
      emit(f,"%s%s",idprefix,p->v->identifier);
    return;
  }
  unsupported_ic(0);
}

/* ------------------------------------------------------------------ */
/* Local-variable / stack-frame addressing.

   The 78312A's only stack-relative addressing mode is [SP+byte]
   ("based addressing", NEC User's Manual sec 4.2.8, mod=0x06) --
   confirmed real hardware, now implemented in vasm's cpu.c. It is
   8-bit only (no word form -- checked every MOVW/ADDW/SUBW/CMPW row
   in shared/Opcodes.h, none take OT_MEM), so a 2-byte local is always
   two separate 8-bit accesses at offset and offset+1.

   Frame layout (matches the standard vbcc convention in
   doc/interface.texi: offset>=0 is a local, offset<0 is a parameter
   at -(offset+maxalign) in the parameter area), low to high address,
   for a stack that grows downward:
     [ outgoing call args, size=argsize ]  <- SP sits here after function_top()
     [ locals, size=localsize ]
     [ return address, size=2 ]   <- pushed by the caller's CALL (confirmed
                                      "SP<-SP-2" in the User's Manual's CALL
                                      instruction reference)
     [ parameters ]
   We keep no frame pointer and no saved-register area (every register
   is caller-saved -- see regscratch in init_cg()), so this is the
   whole of it.

   The "outgoing call args" area exists because the 78312A's only real
   PUSH/POP instructions take a register-LIST bitmask (OT_POST_BYTE --
   see shared/Opcodes.h), which vasm's cpu.c doesn't implement yet, so
   there is no real per-argument push available. Instead -- exactly
   like the FIXED_SP model other vbcc backends use for architectures
   without a hardware push (see machines/minx/machine.c) -- we
   pre-size a single reusable argument-build area (the largest any one
   CALL in this function needs, scanned up front in gen_code()) as
   part of the frame, and each PUSH IC becomes a plain MOV into the
   next slot of it via [SP+byte]. */
static long localsize,argsize,pushed;

static long real_offset(struct obj *o)
{
  long off=zm2l(o->v->offset);
  if(off<0)
    off=localsize+argsize+2-off-zm2l(maxalign);
  else
    off+=argsize;
  off+=zm2l(o->val.vmax);
  return off;
}

/* Forward declaration: emit_var_mem() (needed by load_byte()/store_byte()
   below) must be able to call resolve_pointer(), but resolve_pointer()'s
   own body (defined further down, after load_word()) needs load_word()
   to already be defined -- a genuine mutual dependency between "print a
   memory operand" and "turn a DREFOBJ operand into a dereferenceable
   register", broken here with an ordinary forward declaration. */
static void resolve_pointer(FILE *f,struct obj *o,int t);

/* ------------------------------------------------------------------ */
/* saddr / sfr short-direct addressing for compile-time-constant
   addresses (feature-proposal item 1: "native saddr / SFR addressing").

   Investigated (task #43) before writing any of this: does a constant
   address survive as a KONST-flagged operand all the way to gen_code(),
   or does vbcc's frontend already force it through a register load
   first? Traced resolve_pointer() (above) -- the ONE place a DREFOBJ
   operand's pointer VALUE gets turned into a dereferenceable register:
   it copies o, strips DREFOBJ off the copy (now "the pointer VALUE
   itself", exactly as KONST/VAR/REG as the original expression left
   it), and hands that unconditionally to load_word(f,REG_DE,&tmp,...).
   load_word() already has a KONST branch (a plain "movw de,#addr").
   So the answer is definitive: for something like
   "*(volatile char*)0xFF01 = 5;" (or the SFR8(0xFF01)-style macro the
   feature proposal itself uses as its example), the IC's pointer
   operand keeps BOTH KONST and DREFOBJ set -- confirmed independently
   by emit_obj()'s own existing "(p->flags&(KONST|DREFOBJ))==
   (KONST|DREFOBJ)" check above -- and o->val already holds the literal
   address. No frontend/IC-pipeline change is needed; this is a pure
   backend code-selection addition, exactly like the CALLT slot work.

   The other half of the open question -- can this ALSO cover ordinary
   C globals the linker happens to place in the short-address window --
   is a real "no" worth recording here rather than silently narrowing
   scope: OT_SADDR/OT_SADDR_FE's own vasm-side parser (cpus/78k3/cpu.c)
   requires the address to already be resolved at ASSEMBLE time, one
   file at a time, before vlink ever runs (a saddr byte is a nonlinear
   transform of the address -- see shared/Addressing.h -- not something
   a linker relocation can express, so vasm correctly refuses to gamble
   and falls back to OT_ADDR16 whenever it can't prove the address in
   range right now). An ordinary vbcc-emitted "_myvar" reference is a
   cross-file symbol that vasm can't resolve until link time, so it can
   never take this path no matter what vbcc emits for it -- this is a
   hard boundary of the two-pass assemble-then-link design already used
   throughout this toolchain (same reason CALLT collision detection had
   to move to the linker), not something worth working around with a
   speculative new attribute/linker-relaxation scheme before there's a
   concrete need for it. So: automatic for genuine compile-time-literal
   addresses (the actual SFR8()-macro pattern the proposal cites, and
   any hand-pinned literal pointer into the saddr window) -- not for
   ordinary linker-placed globals, even ones that end up living in short
   RAM. That's a real, separate feature (needs its own attribute design,
   CALLT-style) if it's ever wanted.

   Address windows below are FE20H-FF1FH (saddr) and FF00H-FFFFH (sfr),
   confirmed against shared/Addressing.h (this toolchain's single source
   of truth for the saddr/sfr <-> byte transforms) and independently
   matching vasm's own OT_SADDR/OT_SFR range checks in cpus/78k3/cpu.c.
   The two windows overlap in FF00H-FF1FH; a bare address there is left
   to vasm's own operand search, which already tries the compact
   OT_SADDR row before falling back to OT_ADDR16 (see cpu.c's OT_SADDR
   parsing comment) -- so emit_const_addr() below never needs to
   duplicate that choice, only decide "sfr[] syntax" vs "bare address"
   syntax, which is the one distinction vasm's parser can't infer from
   a plain number alone (OT_SFR/OT_SFRP require the literal "sfr[...]"
   text; nothing auto-promotes a bare address into that form). */
#define SADDR_WINDOW_LO 0xfe20UL
#define SADDR_WINDOW_HI 0xff1fUL
#define SFR_WINDOW_LO   0xff00UL
#define SFR_WINDOW_HI   0xffffUL

static int const_addr_in_short_window(unsigned long addr)
{
  return (addr>=SADDR_WINDOW_LO&&addr<=SADDR_WINDOW_HI)||
         (addr>=SFR_WINDOW_LO&&addr<=SFR_WINDOW_HI);
}

/* Emits the assembler-text memory operand for a literal address already
   confirmed in-window by const_addr_in_short_window(). FF20H-FFFFH is
   sfr-only territory (past saddr's FF1FH ceiling), so it MUST use the
   explicit "sfr[addr]" syntax -- the only text OT_SFR/OT_SFRP's parser
   accepts (cpu.c: requires a literal "sfr[" prefix and "]" suffix, no
   bare-address fallback). Everywhere else in-window (FE20H-FF1FH,
   which includes the FF00H-FF1FH sub-range sfr can also reach) a bare
   address is both valid and already preferred by vasm's own operand
   search over the generic OT_ADDR16 form -- see the comment above. */
static void emit_const_addr(FILE *f,unsigned long addr)
{
  if(addr>SADDR_WINDOW_HI)
    emit(f,"sfr[0x%lx]",addr);
  else
    emit(f,"0x%lx",addr);
}

/* True and address-returning iff o is a dereferenced (DREFOBJ) literal
   constant pointer (KONST) whose value falls in one of the two short-
   addressing windows -- the ONE shape this fast path applies to. Must
   be checked BEFORE resolve_pointer() touches o: resolve_pointer()
   rewrites o in place to REG|DREFOBJ/REG_DE, permanently discarding the
   KONST value this needs. byteoff (0 or 1) lets word callers ask for
   the high byte's address (addr+1) directly, matching emit_var_mem()'s
   existing byteoff convention for a word object's second byte -- both
   bytes of a word access must independently be in-window for this to
   apply (an address one byte from either window's edge would otherwise
   have its low byte in-window and its high byte not, which per-function
   correctly falls back to the generic path below instead of a mismatched
   half-optimization). */
static int const_deref_addr(struct obj *o,long byteoff,unsigned long *addr)
{
  if((o->flags&(KONST|DREFOBJ))!=(KONST|DREFOBJ)) return 0;
  *addr=((unsigned long)zm2l(o->val.vmax)+(unsigned long)byteoff)&0xffffUL;
  return const_addr_in_short_window(*addr);
}

/* Word variant: a single MOVW-family instruction (saddrp/sfrp) takes
   ONE base address and moves both bytes in one shot -- a real win over
   the generic path's two separate byte MOVs through DE (4 bytes/2
   instructions there vs 2 bytes/1 instruction here for the saddrp
   form). This must NOT just reuse const_deref_addr()'s window check on
   the base alone: saddrp's own valid window (FE20H-FF1EH, even bases
   only -- see cpu.c's OT_SADDRP parsing) is narrower than plain saddr's
   FF1FH ceiling, so a base right at that edge needs checking as a
   whole word, not one byte. Requiring BOTH bytes independently in-
   window (via two const_deref_addr() calls) plus an even base sidesteps
   the mismatch entirely: the only base const_deref_addr(...,0,...)
   would accept that saddrp itself would reject is the odd address
   FF1FH, which the evenness check below rejects directly -- so
   whatever's left standing is always a base emit_const_addr() picks
   the correct syntax for (bare address for FE20H-FF1EH, "sfr[]" for
   FF20H and up; the FF00H-FF1EH overlap is left to vasm's own operand
   search exactly as the byte case's comment describes). */
static int const_deref_addr_word(struct obj *o,unsigned long *addr)
{
  unsigned long lo,hi;
  if(!const_deref_addr(o,0,&lo)) return 0;
  if(!const_deref_addr(o,1,&hi)) return 0;
  if(lo&1) return 0; /* saddrp/sfrp both take one aligned base -- see above */
  *addr=lo;
  return 1;
}
/* ------------------------------------------------------------------ */

/* Prints the "[sp+N]" / "label" / "label+1" / "[de+N]" text for a memory
   operand at a given byte offset within it (0 for a char or the low
   byte of a word, 1 for a word's high byte). A DREFOBJ operand (pointer
   dereference, e.g. "*p") is resolved into the fixed DE scratch pair
   first (see resolve_pointer()) and printed as [de+byteoff] -- a real,
   directly-encodable addressing mode: shared/Opcodes.h's OP_MEM_MOD is
   accepted by MOV/XCH/ADD/ADDC/SUB/SUBC/AND/OR/XOR/CMP with ANY valid
   [reg+byte] base register (not just SP), confirmed by grepping every
   OP_MEM_MOD row -- so this needs no scratch-register bounce beyond
   getting the pointer itself into DE. */
static void emit_var_mem(FILE *f,struct obj *o,long byteoff)
{
  /* An address is not a memory operand: VARADR must go through
     load_word() (or emit_obj()), never here. */
  if((o->flags&(VARADR|DREFOBJ))==VARADR) ierror(0);
  {
    unsigned long addr;
    if(const_deref_addr(o,byteoff,&addr)){
      emit_const_addr(f,addr);
      return;
    }
  }
  if(o->flags&DREFOBJ){
    resolve_pointer(f,o,POINTER);
    emit(f,"[%s+%ld]",regnames[o->reg],byteoff);
    return;
  }
  if(isauto(o->v->storage_class)){
    long off=real_offset(o)+byteoff;
    if(off<0||off>255){
      fprintf(stderr,"78k3 backend: stack-frame offset %ld for variable "
                     "\"%s\" is out of [sp+byte]'s 0-255 range -- frame "
                     "too large for based addressing\n",
                     off,o->v->identifier?o->v->identifier:"?");
      ierror(0);
    }
    emit(f,"[sp+%ld]",off);
  }else if(o->v->storage_class==STATIC){
    /* BUG FIX 2026-09-30: o->val.vmax carries this operand's own
       compile-time constant offset (a constant array index times
       element size, or a struct-member offset) -- exactly what
       real_offset() above adds in for the auto/stack case. This branch
       used to add only byteoff (the 0/1 low/high-byte selector word
       callers pass) and silently drop val.vmax entirely, so EVERY
       constant-indexed element of a static/global array (or constant-
       offset member of a static/global struct) compiled to the same
       address as element/member 0 -- confirmed via emu78k3 regression
       testing: "result[1]=22; result[2]=33;" on a static array both
       compiled to the identical "mov l1,a" as result[0], clobbering
       byte 0 three times instead of writing bytes 0/1/2. Reads
       (load_byte/load_word) go through this same function and were
       equally affected. A variable (non-constant) index never reaches
       this path at all (it resolves via the DREFOBJ/resolve_pointer
       branch above instead), so this fix is scoped exactly to the
       constant-offset case that was broken. */
    long off=zm2l(o->val.vmax)+byteoff;
    emit(f,"%s%ld",labprefix,zm2l(o->v->offset));
    if(off) emit(f,"+%ld",off);
  }else{
    long off=zm2l(o->val.vmax)+byteoff;
    emit(f,"%s%s",idprefix,o->v->identifier);
    if(off) emit(f,"+%ld",off);
  }
}

/* ------------------------------------------------------------------ */
/* Register load/store helpers.

   Hardware constraints these all work around (verified against
   shared/Opcodes.h + shared/RegisterNames.h, not assumed):
    - Every memory<->register 8-bit MOV (saddr/addr16/our new [sp+byte])
      is fixed to the A register on the register side -- there is no
      "mov [addr],x" or "mov h,[addr]" form. So all memory traffic
      funnels through A.
    - The general "mov r,r1" register-register form can put A's value
      into ANY of our 8 byte registers (r spans all 16 hw slots), but
      the reverse -- using d/e/h/l as the SOURCE r1 -- does not exist
      (r1 only spans x/a/c/b). regok() already keeps scalar chars out
      of d/e/h/l for exactly this reason; the only way to move a value
      that lives in d/e/h/l is as part of a whole register-pair MOVW. */

static int byte_reg_ok(int r)
{
  return r==REG_X||r==REG_A||r==REG_C||r==REG_B;
}

/* Loads obj o (REG/KONST/VAR, 1 byte) into byte register destreg.

   HAZARD for callers needing TWO byte values live at once in A and
   some OTHER register: every memory/KONST/DREFOBJ source is fixed-A
   (see byte_reg_ok()'s comment above), so for any destreg other than
   A this function loads through A FIRST and only THEN copies A into
   destreg -- meaning it clobbers whatever was already in A as a side
   effect. A caller that has already put a value in A and then calls
   load_byte(f, some-other-register, ...) will find A holding the
   SECOND value afterward, not the first. The safe pattern is to load
   every non-A destination FIRST, and load (or set) A LAST, right
   before the value in A is actually needed. This is not hypothetical:
   an earlier version of MINUS's byte case got this backwards (set
   A=0, then load_byte'd q1 into C, silently losing the 0) and always
   returned 0 -- caught only once a real test (t8.c) exercised unary
   minus on a char, which no earlier test had done. Every later
   caller needing two live byte values (MULT's byte case included)
   follows the load-non-A-first ordering for exactly this reason. */
static void load_byte(FILE *f,int destreg,struct obj *o,int t)
{
  if((o->flags&(REG|DREFOBJ))==REG){
    if(o->reg==destreg) return;
    if(!byte_reg_ok(o->reg)) ierror(0); /* CHAR values never live in d/e/h/l -- see regok() */
    emit(f,"\tmov\t%s,%s\n",regnames[destreg],regnames[o->reg]);
    return;
  }
  {
    unsigned long addr;
    if(const_deref_addr(o,0,&addr)){
      emit(f,"\tmov\ta,");
      emit_const_addr(f,addr);
      emit(f,"\n");
      if(destreg!=REG_A) emit(f,"\tmov\t%s,a\n",regnames[destreg]);
      return;
    }
  }
  if(o->flags&DREFOBJ){
    resolve_pointer(f,o,t);
    emit(f,"\tmov\ta,[%s+0]\n",regnames[o->reg]);
    if(destreg!=REG_A) emit(f,"\tmov\t%s,a\n",regnames[destreg]);
    return;
  }
  if(destreg!=REG_A){
    load_byte(f,REG_A,o,t);
    emit(f,"\tmov\t%s,a\n",regnames[destreg]);
    return;
  }
  if(o->flags&KONST){
    emit(f,"\tmov\ta,#");
    emitval(f,&o->val,t&NU);
    emit(f,"\n");
    return;
  }
  if(o->flags&VAR){
    emit(f,"\tmov\ta,");
    emit_var_mem(f,o,0);
    emit(f,"\n");
    return;
  }
  unsupported_ic(0);
}

/* Stores byte register srcreg into obj o (REG/VAR, 1 byte).

   CALLER CONTRACT: srcreg must be r1-eligible (REG_X/REG_A/REG_C/
   REG_B) -- every branch below eventually reads FROM srcreg via a
   "mov <dest>,srcreg" or "mov a,srcreg" r1-source form, and per the
   R1 restriction documented above regok() and byte_reg_ok(), d/e/h/l
   can never be read that way (confirmed failing directly via vasm:
   "mov a,h"/"mov a,l"/"mov a,d"/"mov a,e" all give "illegal operand
   types"). Every existing call site already only ever passes X/A/C/B
   (guaranteed for register-variable sources by regok() itself, which
   forbids CHAR values from ever living in d/e/h/l -- see its own
   comment), so this was never exercised; it is asserted here rather
   than silently mishandled, because the fix a caller needs (relocate
   the byte out of its pair via a whole-register MOVW, which is NOT
   r1-restricted -- confirmed separately) depends on which pair and
   which half, information this function does not have once it's just
   been handed a single register number. Task #16's DIV/MOD is the
   first code that has to do this relocation itself, right before
   calling store_byte. */
static void store_byte(FILE *f,int srcreg,struct obj *o,int t)
{
  if(!byte_reg_ok(srcreg)) ierror(0);
  if((o->flags&(REG|DREFOBJ))==REG){
    if(o->reg==srcreg) return;
    if(!byte_reg_ok(o->reg)) ierror(0);
    emit(f,"\tmov\t%s,%s\n",regnames[o->reg],regnames[srcreg]);
    return;
  }
  {
    unsigned long addr;
    if(const_deref_addr(o,0,&addr)){
      if(srcreg!=REG_A){
        emit(f,"\tmov\ta,%s\n",regnames[srcreg]);
        srcreg=REG_A;
      }
      emit(f,"\tmov\t");
      emit_const_addr(f,addr);
      emit(f,",a\n");
      return;
    }
  }
  if(o->flags&DREFOBJ){
    /* HAZARD, same root cause as load_word()'s own documented one:
       resolve_pointer() internally calls load_word(DE,...) to fetch
       o's pointer VALUE when it's VAR-class (a plain pointer variable
       -- the common case, e.g. a struct-pointer parameter), and THAT
       bounces through register A -- silently destroying srcreg's value
       if srcreg is REG_A, before it's ever written to memory. Stash it
       into C FIRST (resolve_pointer's VAR-fetch only ever touches
       A/D/E, never C -- see load_word()'s VAR branch) if it's actually
       in A; nothing to do otherwise. Confirmed as a real bug via
       t19.c's set_x(struct point*,int): *p=v (word ASSIGN into a
       dereferenced struct-pointer field) corrupted the high byte of
       the stored value whenever p was a VAR-class pointer -- same
       family as the load_word() bug fixed for ADDI2P/COMPARE/ADD/SUB,
       just reached through store_word()/store_byte() -> resolve_
       pointer() -> load_word() instead of two sibling call-site loads. */
    if(srcreg==REG_A){
      emit(f,"\tmov\tc,a\n");
      srcreg=REG_C;
    }
    resolve_pointer(f,o,t);
    if(srcreg!=REG_A){
      emit(f,"\tmov\ta,%s\n",regnames[srcreg]);
      srcreg=REG_A;
    }
    emit(f,"\tmov\t[%s+0],a\n",regnames[o->reg]);
    return;
  }
  if(srcreg!=REG_A){
    emit(f,"\tmov\ta,%s\n",regnames[srcreg]);
    srcreg=REG_A;
  }
  if(o->flags&VAR){
    emit(f,"\tmov\t");
    emit_var_mem(f,o,0);
    emit(f,",a\n");
    return;
  }
  unsupported_ic(0);
}

/* Loads obj o (REG/KONST/VAR, 2 bytes) into register-pair destpair.

   HAZARD for callers needing TWO word values live at once, one already
   in AX and this one going into some OTHER pair (HL/BC/etc): the VAR
   and DREFOBJ branches below always bounce both bytes through register
   A, *regardless of destpair* -- exactly the same class of hazard
   load_byte() documents above byte_reg_ok(), just less obvious here
   because destpair being non-A-named (HL, say) doesn't visually hint
   that A gets touched. A caller that already loaded a first word into
   AX and then calls load_word(f, HL-or-other, ...) for a VAR/DREFOBJ
   second operand will find register A (and therefore AX's high byte)
   silently overwritten with the SECOND operand's high byte afterward.
   This was a REAL bug, shipped since tasks #12/#15 in COMPARE and ADD/
   SUB's word cases (both did "load_word(AX,q1) then load_word(HL,q2)"
   whenever q2 was VAR/DREFOBJ) -- masked whenever q1 and q2 happened
   to share the same high byte (e.g. any two ints under 256), which is
   why it went uncaught for so long despite affecting int addition and
   comparison between two runtime values, arguably the single most
   common operation in the language. Caught only once task #20's
   ADDI2P needed the identical pattern for array indexing with a
   runtime-variable index (t18.c's idx_int()), traced with a step-by-
   step simulator dump, and confirmed retroactively by re-simulating
   the OLD .s output for add3()/add_through_ptr()/compare_two_ptrs()
   against known values with differing high bytes. The safe pattern
   (already used correctly by MULT/DIV/MOD/gen_word_bitwise all along):
   load the VAR/DREFOBJ-class operand into its non-AX pair FIRST, then
   load the other operand into AX LAST, right before it's needed --
   never the reverse. This function's own internals are unchanged (the
   bounce-through-A is fine, even necessary, for a memory operand in
   isolation); the fix belongs entirely at each call site's ordering. */
static void load_word(FILE *f,int destpair,struct obj *o,int t)
{
  struct rpair rp;
  if((o->flags&(REG|DREFOBJ))==REG){
    if(o->reg==destpair) return;
    emit(f,"\tmovw\t%s,%s\n",regnames[destpair],regnames[o->reg]);
    return;
  }
  {
    /* MOVW to/from saddrp/sfrp only ever names AX as the register
       operand (confirmed by grepping every OP_SADDRP_2/OP_SFRP_2 row
       in shared/Opcodes.h's MOVW entries -- none pair with OP_BC/OP_DE/
       OP_HL, only OP_AX), so this fast path is restricted to
       destpair==REG_AX; any other destpair (e.g. the REG_HL scratch
       pair ADD/SUB/CMP's word case loads a DREFOBJ q2 into) falls
       through to the always-correct generic DE-bounce path below
       unchanged. */
    unsigned long addr;
    if(destpair==REG_AX&&const_deref_addr_word(o,&addr)){
      emit(f,"\tmovw\tax,");
      emit_const_addr(f,addr);
      emit(f,"\n");
      return;
    }
  }
  if(o->flags&DREFOBJ){
    resolve_pointer(f,o,t);
    /* resolve_pointer() always lands the pointer in DE (see its own
       comment) -- destpair must not also be DE, or storing the low
       byte into E would corrupt the very address the high-byte read
       then depends on. No existing call site passes REG_DE, but this
       is cheap insurance against a future one that would silently
       miscompile. */
    if(destpair==REG_DE) ierror(0);
    reg_pair(destpair,&rp);
    emit(f,"\tmov\ta,[%s+0]\n",regnames[o->reg]);
    emit(f,"\tmov\t%s,a\n",regnames[rp.r2]);
    emit(f,"\tmov\ta,[%s+1]\n",regnames[o->reg]);
    emit(f,"\tmov\t%s,a\n",regnames[rp.r1]);
    return;
  }
  if(o->flags&KONST){
    emit(f,"\tmovw\t%s,#",regnames[destpair]);
    emitval(f,&o->val,t&NU);
    emit(f,"\n");
    return;
  }
  if((o->flags&(VAR|VARADR|DREFOBJ))==(VAR|VARADR)){
    emit(f,"\tmovw\t%s,#",regnames[destpair]);
    emit_varadr(f,o);
    emit(f,"\n");
    return;
  }
  if(o->flags&VAR){
    if(ISFUNC(o->v->vtyp->flags)){
      /* A bare reference to a FUNCTION (used as a value -- passed as a
         callback, assigned to a function pointer -- not called, and
         without an explicit &) decays to its address; there is no
         "data value stored at the function's label" to read, unlike
         an ordinary VAR. This mirrors ADDRESS's own static-storage
         path (movw dest,#label) rather than falling through to the
         two-byte memory-read logic below, which would (and did,
         before this fix -- confirmed via a live test) treat the
         function's own CODE bytes as if they were a stored data
         value: passing a bare function name as an argument (e.g.
         call_through_ptr(add1, x)) generated "mov a,_add1 / mov
         a,_add1+1" -- reading two bytes of add1's machine code as the
         supposed pointer value -- instead of loading &add1. */
      emit(f,"\tmovw\t%s,#",regnames[destpair]);
      emit_obj(f,o,POINTER);
      emit(f,"\n");
      return;
    }
    reg_pair(destpair,&rp);
    /* low byte -> a -> rp.r2 (low half); high byte -> a -> rp.r1 (high
       half) -- every memory read is fixed-A, so both bytes go through
       a in turn (see the comment above byte_reg_ok()). */
    emit(f,"\tmov\ta,");emit_var_mem(f,o,0);emit(f,"\n");
    emit(f,"\tmov\t%s,a\n",regnames[rp.r2]);
    emit(f,"\tmov\ta,");emit_var_mem(f,o,1);emit(f,"\n");
    emit(f,"\tmov\t%s,a\n",regnames[rp.r1]);
    return;
  }
  unsupported_ic(0);
}

/* Stores register-pair srcpair into obj o (REG/VAR, 2 bytes).

   CALLER CONTRACT (VAR and DREFOBJ destinations only -- the plain REG
   destination branch just below uses a whole-pair MOVW and is fine
   with any pair): srcpair must be REG_AX or REG_BC. The VAR/DREFOBJ
   branches extract each byte via "mov a,rp.r1"/"mov a,rp.r2", an
   r1-source MOV, and per the R1 restriction (see byte_reg_ok()'s
   comment and store_byte()'s) only x/a/c/b are ever valid there --
   AX=(a,x) and BC=(b,c) both qualify, but DE=(d,e) and HL=(h,l) do
   not (confirmed failing directly via vasm). This shipped silently
   broken for REG_HL/REG_DE through tasks #12 and #15 because every
   call site so far always passed REG_AX; caught only once task #16's
   DIV/MOD needed to store a result living in HL. As with
   store_byte(), the fix (relocate the whole pair into AX or BC via a
   plain MOVW, which is not r1-restricted) is left to the caller,
   which knows the register's provenance; this is asserted rather than
   silently mishandled. */
static void store_word(FILE *f,int srcpair,struct obj *o,int t)
{
  struct rpair rp;
  if((o->flags&(REG|DREFOBJ))==REG){
    if(o->reg==srcpair) return;
    emit(f,"\tmovw\t%s,%s\n",regnames[o->reg],regnames[srcpair]);
    return;
  }
  {
    /* Same AX-only restriction as load_word()'s matching fast path --
       see its comment. srcpair!=REG_AX (e.g. REG_BC, MINUS's word case
       via load_word(HL,...)+subw+store_word(AX,...) always uses AX
       here, but a future/other caller might not) falls through to the
       generic path unchanged rather than risk emitting a "movw
       saddrp,bc"-shaped line no mnemonic row actually matches. */
    unsigned long addr;
    if(srcpair==REG_AX&&const_deref_addr_word(o,&addr)){
      emit(f,"\tmovw\t");
      emit_const_addr(f,addr);
      emit(f,",ax\n");
      return;
    }
  }
  if(o->flags&DREFOBJ){
    /* HAZARD: resolve_pointer() internally calls load_word(DE,...) to
       fetch o's pointer VALUE when it's VAR-class (the common case --
       e.g. a struct-pointer parameter), and that bounces through
       register A -- silently destroying srcpair's HIGH byte (register
       A, which AX's high half literally is) before it's ever written
       out, if srcpair is REG_AX. Relocate the whole pair into BC
       FIRST in that case (a plain whole-pair movw, safe -- resolve_
       pointer()'s VAR-fetch never touches B or C) so the value being
       stored survives the call; srcpair==REG_BC needs no such stash
       (already safe). See store_byte()'s matching comment -- same
       root cause, confirmed the same way via t19.c's set_x(). */
    if(srcpair==REG_AX){
      emit(f,"\tmovw\tbc,ax\n");
      srcpair=REG_BC;
    }
    resolve_pointer(f,o,t);
    /* see the matching check in load_word(): srcpair==REG_DE would
       corrupt the very pointer resolve_pointer() just placed in DE.
       srcpair==REG_HL is additionally rejected here as an r1-source
       violation (see the function comment above). */
    if(srcpair==REG_DE||srcpair==REG_HL) ierror(0);
    reg_pair(srcpair,&rp);
    emit(f,"\tmov\ta,%s\n",regnames[rp.r1]);
    emit(f,"\tmov\t[%s+1],a\n",regnames[o->reg]);
    emit(f,"\tmov\ta,%s\n",regnames[rp.r2]);
    emit(f,"\tmov\t[%s+0],a\n",regnames[o->reg]);
    return;
  }
  if(o->flags&VAR){
    if(srcpair==REG_DE||srcpair==REG_HL) ierror(0); /* see function comment */
    reg_pair(srcpair,&rp);
    /* rp.r1 (high half) -> a -> high byte; rp.r2 (low half) -> a -> low
       byte -- every memory write is fixed-A too. */
    emit(f,"\tmov\ta,%s\n",regnames[rp.r1]);
    emit(f,"\tmov\t");emit_var_mem(f,o,1);emit(f,",a\n");
    emit(f,"\tmov\ta,%s\n",regnames[rp.r2]);
    emit(f,"\tmov\t");emit_var_mem(f,o,0);emit(f,",a\n");
    return;
  }
  unsupported_ic(0);
}

/* Resolves a DREFOBJ operand (o->flags&DREFOBJ set -- "the object o
   points to", per interface.texi) into a directly dereferenceable
   register: always REG_DE afterward, with o rewritten in place to
   REG|DREFOBJ so callers can just use o->reg. This is the ONE place
   that turns a pointer VALUE (however it's held: VAR in memory, KONST
   absolute address, or already REG in some register) into something
   [reg+byte] addressing can use.

   Only DE, SP and HL are valid [reg+byte] base registers (shared/
   Opcodes.h's OT_MEM rows via the MEM_REGS table also used by the
   independent sweep-test: de/sp/hl/up/vp -- we don't model the
   78312A's UP/VP system registers as general storage, and SP is
   reserved for our own frame bookkeeping), so a pointer resident in
   AX or BC must be moved before it can be dereferenced regardless.

   DE (not HL) is the fixed target deliberately: HL is already used
   elsewhere in gen_code() as the general scratch pair for holding an
   OPERAND'S VALUE (e.g. the word ADD/SUB/COMPARE cases' "load q2 into
   HL when it's a memory operand" fallback, since ADDW/SUBW/CMPW have
   no memory form). If a dereferenced q2's pointer were ALSO placed in
   HL, reading its two bytes into HL (the value destination) would
   overwrite L after the first byte, corrupting the address before the
   second byte could be read. Using a separate, dedicated pointer
   register avoids that entirely, and every caller (load_byte/
   store_byte/load_word/store_word) already fully consumes one
   operand's dereference before the next is resolved, so DE is never
   needed for two live pointers at once -- confirmed by tracing the
   call order in every gen_code() case that can have two DREFOBJ
   operands (q1 then q2, or q1/q2 then z). */
static void resolve_pointer(FILE *f,struct obj *o,int t)
{
  if((o->flags&(REG|DREFOBJ))==(REG|DREFOBJ)&&o->reg==REG_DE)
    return; /* already exactly where we need it */
  {
    struct obj tmp=*o;
    tmp.flags&=~DREFOBJ; /* now refers to the pointer VALUE itself */
    load_word(f,REG_DE,&tmp,POINTER);
  }
  o->flags=REG|DREFOBJ;
  o->reg=REG_DE;
}

static int is_word_type(int t)
{
  t&=NQ;
  return t==SHORT||t==INT||t==POINTER;
}

/* Bounds-checked "[sp+N]" text for a raw numeric offset -- used for
   the outgoing-call argument-build slots (PUSH), which are not VAR
   objects (no struct Var/real_offset() to go through), just plain
   known-at-compile-time byte offsets from the current SP. */
static void emit_sp_off(FILE *f,long off)
{
  if(off<0||off>255){
    fprintf(stderr,"78k3 backend: argument-build-area offset %ld is out "
                   "of [sp+byte]'s 0-255 range -- frame too large for "
                   "based addressing\n",off);
    ierror(0);
  }
  emit(f,"[sp+%ld]",off);
}

static void store_byte_at_sp(FILE *f,int srcreg,long off)
{
  if(!byte_reg_ok(srcreg)) ierror(0); /* r1-source restriction -- see store_byte() */
  if(srcreg!=REG_A){ emit(f,"\tmov\ta,%s\n",regnames[srcreg]); srcreg=REG_A; }
  emit(f,"\tmov\t");emit_sp_off(f,off);emit(f,",a\n");
}

static void store_word_at_sp(FILE *f,int srcpair,long off)
{
  struct rpair rp;
  if(srcpair==REG_DE||srcpair==REG_HL) ierror(0); /* r1-source restriction -- see store_word() */
  reg_pair(srcpair,&rp);
  emit(f,"\tmov\ta,%s\n",regnames[rp.r1]);
  emit(f,"\tmov\t");emit_sp_off(f,off+1);emit(f,",a\n");
  emit(f,"\tmov\ta,%s\n",regnames[rp.r2]);
  emit(f,"\tmov\t");emit_sp_off(f,off);emit(f,",a\n");
}

/* ------------------------------------------------------------------ */
/* Function entry/exit: allocate/free the stack frame.

   The 78312A can only move a value into SP from AX (MOVW SP,AX) or a
   compile-time-absolute word (MOVW SP,#word -- useless here, our
   frame size is relative to whatever SP the caller handed us), and
   can only add/subtract a word immediate into AX (ADDW/SUBW AX,#word)
   -- confirmed by grepping every OT_SP and M_ADDW/M_SUBW row in
   shared/Opcodes.h. So allocating the frame is:
       movw ax,sp / subw ax,#localsize / movw sp,ax
   Freeing it in function_bottom() has one more wrinkle: by the time
   we get there, AX may already hold this function's return value
   (freturn() always uses AX -- see above). We can't clobber it, and
   there's no way to move anything but AX into SP, so we bounce the
   return value through HL (a plain register-to-register MOVW, safe
   since every register is caller-saved and the function is about to
   return anyway) for the duration of the SP math. */
/* __interrupt prologue/epilogue: unconditionally save/restore PSW (flags)
   plus all four general-register pairs (AX/BC/DE/HL), never a
   regused[]-driven subset.

   machines/c16x/machine.c's INTERRUPT prologue (the reference this was
   checked against) saves its two extra fixed registers (MDL/MDH) always,
   but general registers only via the SAME `regused[i]&&!regscratch[i]&&
   !regsa[i]` loop an ordinary c16x function's prologue already uses --
   i.e. only registers regs.c actually decided to keep live across ICs.
   That doesn't transfer here: this backend sets regsa[i]=1 for X,A,C,B,
   E,D,L,H project-wide (see the register-file comment above msizetab/
   regsa) precisely because every memory-access MOV on this CPU needs one
   of them as mandatory transient scratch, so the allocator never keeps
   values live in them across ICs in the first place -- the c16x-style
   loop's condition (`!regsa[i]`) would be false for every one of them,
   always, making it save nothing. Since ANY of AX/BC/DE/HL could be live
   in whatever code got interrupted (that code doesn't know or care that
   this backend treats them as scratch), the only correct answer is to
   save all four pairs, every time, regardless of what this particular
   ISR's own body happens to touch.

   PSW is saved first (function_top()) and restored last
   (function_bottom()), bracketing everything else -- including the
   frame-alloc/dealloc SUBW/ADDW math, which sets flags as a side effect
   -- so the interrupted code's flags come back exactly as they were,
   undisturbed by any of this function's own arithmetic. */
/* Reproduces exactly the label text function_top() below emits for v, so
   a __callt_slotN table entry's ".word" can reference the same symbol.
   Same two cases as function_top()'s own label emission: EXTERN
   (idprefix+identifier) vs static (labprefix+numeric offset). */
static char *callt_symbol_text(struct Var *v)
{
  char buf[64];
  if(v->storage_class==EXTERN)
    sprintf(buf,"%s%s",idprefix,v->identifier);
  else
    sprintf(buf,"%s%ld",labprefix,zm2l(v->offset));
  return mystrdup(buf);
}

static void function_top(FILE *f,struct Var *v,long offset)
{
  int is_interrupt=(v->tattr&INTERRUPT)!=0;
  int callt_slot=callt_resolve(v);
  if(section!=CODE){emit(f,textname);section=CODE;}
  if(v->storage_class==EXTERN)
    emit(f,"\t.global\t%s%s\n%s%s:\n",idprefix,v->identifier,idprefix,v->identifier);
  else
    emit(f,"%s%ld:\n",labprefix,zm2l(v->offset));
  if(callt_slot>=0){
    char *sym=callt_symbol_text(v);
    callt_slot_claim(callt_slot,v->identifier?v->identifier:"?",sym);
  }
  if(is_interrupt){
    emit(f,"\tpush\tpsw\n");
    emit(f,"\tpush\t{ax,bc,de,hl}\n");
  }
  if(offset!=0){
    emit(f,"\tmovw\tax,sp\n");
    emit(f,"\tsubw\tax,#%ld\n",offset);
    emit(f,"\tmovw\tsp,ax\n");
  }
}

static void function_bottom(FILE *f,struct Var *v,long offset)
{
  int is_interrupt=(v->tattr&INTERRUPT)!=0;
  if(offset!=0){
    if(is_interrupt){
      /* No return value to bounce through HL here (__interrupt functions
         are void, and even a nonvoid AX result would just be clobbered by
         the pop{ax,bc,de,hl} below regardless) -- plain 3-instruction
         frame-dealloc, AX is free to use as scratch. */
      emit(f,"\tmovw\tax,sp\n");
      emit(f,"\taddw\tax,#%ld\n",offset);
      emit(f,"\tmovw\tsp,ax\n");
    }else{
      emit(f,"\tmovw\thl,ax\n");
      emit(f,"\tmovw\tax,sp\n");
      emit(f,"\taddw\tax,#%ld\n",offset);
      emit(f,"\tmovw\tsp,ax\n");
      emit(f,"\tmovw\tax,hl\n");
    }
  }
  if(is_interrupt){
    emit(f,"\tpop\t{ax,bc,de,hl}\n");
    emit(f,"\tpop\tpsw\n");
    emit(f,"\treti\n");
  }else{
    emit(f,"\tret\n");
  }
}

/* ------------------------------------------------------------------ */
/* Conditional branches: NEC User's Manual sec 6.20 gives each branch's
   exact flag condition, confirmed there (not assumed) rather than
   guessed from other CPUs' usual carry/zero/sign conventions:
     BE/BNE     Z=1 / Z=0                       (signedness-independent)
     BLT/BGE    (P/V xor S)=1 / =0              (signed)
     BLE/BGT    (P/V xor S) or Z = 1 / (...)=0  (signed)
     BL/BNL     CY=1 / CY=0                     (unsigned "below")
     BH/BNH     (Z or CY)=0 / =1                (unsigned "above")
   vbcc's BEQ/BNE/BLT/BGE/BLE/BGT IC codes are consecutive in exactly
   this order (BEQ..BGT), matched against the type's UNSIGNED flag. */
static const char *cond_branch_mnemonic(int code,int type)
{
  int uns=(type&UNSIGNED)!=0;
  switch(code){
  case BEQ: return "be";
  case BNE: return "bne";
  case BLT: return uns?"bl":"blt";
  case BGE: return uns?"bnl":"bge";
  case BLE: return uns?"bnh":"ble";
  case BGT: return uns?"bh":"bgt";
  }
  return 0;
}

/* BUG FIX (vbcc78k3_COMPILER_BUGS.md, bug #1 -- found via real-hardware
   bring-up of firmware/panelprobe BETA_32, a `u8 sub; if(sub>13)...`
   guard that silently let every candidate >= 0x80 through instead of
   refusing it): the call site below, in gen_code()'s BEQ..BGT case,
   used to pass p->typf straight to cond_branch_mnemonic() as "the
   operand type" to decide signedness. For a conditional-branch IC,
   p->typf is NOT type information at all -- per supp.h's own
   #define iclabel(p) (p->typf), and confirmed directly against this
   backend's own LABEL/BRA cases two lines above (which print p->typf
   as a plain branch-target label number) -- it is the jump target's
   label number. Reading a label's numeric value as a type bitmask and
   testing its UNSIGNED bit (256, i.e. bit 8) is pure chance: whichever
   way the compiler happened to number that particular label decided
   whether BLE/BGT/BLT/BGE came out signed or unsigned, with zero
   relation to the actual operands. Every relational comparison this
   backend has ever emitted for a value that could reach 0x80 (u8) or
   0x8000 (u16) was exposed to this, though it went unnoticed for many
   betas because every PRIOR such comparison happened to stay under
   that boundary, where signed and unsigned reads of the same bit
   pattern agree and the bug is invisible.

   Fix: the real unsignedness is already computed correctly, once,
   right where the preceding COMPARE/TEST IC emits its `cmp`/`cmpw`
   instruction (the local `t` in gen_code()'s per-IC loop, taken
   directly from THAT IC's own typf, which for COMPARE/TEST genuinely
   does mean "type of the operands" -- the overload only afflicts
   branch-class ICs). last_compare_was_unsigned stashes that single
   bit across to the very next IC, which per this backend's own output
   (confirmed by scanning its generated ICs: every BLT/BGE/BLE/BGT/
   BEQ/BNE immediately follows the COMPARE or TEST that set the flags
   it reads, with no intervening IC of any kind -- NOP/FREEREG/
   ALLOCREG are bookkeeping-only and never appear between them either)
   is always that exact branch. BEQ/BNE ignore it entirely (their
   mnemonic doesn't depend on signedness), so there is no case where a
   stale value from an unrelated earlier compare could leak into a
   later BLT/BGE/BLE/BGT: the flag is always freshly set by the one
   COMPARE/TEST immediately before whichever branch reads it. */
static int last_compare_was_unsigned=0;

/* ------------------------------------------------------------------ */
/* DIV/MOD helpers.

   Hardware provides only unsigned divide (DIVUW: word AX / byte r1 ->
   AX quotient, r1 remainder; DIVUX: 32-bit AXDE / word rp1 -> AXDE
   quotient, rp1 remainder -- confirmed against the real instruction
   manual; no signed divide exists at all, unlike MULT which could
   reuse the unsigned multiply via truncation invariance). Division
   has no equivalent trick -- signed quotient/remainder genuinely need
   sign correction: divide the absolute values, then negate the
   quotient if the operand signs differed and negate the remainder if
   the dividend (q1) was negative (C truncates toward zero, so the
   remainder always carries the dividend's sign). Negating via 0-x in
   two's complement also handles INT_MIN/CHAR_MIN correctly for free:
   0-(-32768) wraps to 32768's correct unsigned bit pattern (0x8000).

   The sign-correction is a 4-way branch (test q1's sign, then within
   each side test q2's sign) with neg_quot/neg_rem baked in as
   compile-time-literal booleans per leaf, rather than a runtime sign
   flag threaded through the whole sequence -- there is no spare
   register to hold such a flag across the divide itself, since AX/DE
   (dividend) and BC or HL (divisor/remainder) already consume the
   entire register file. This does mean the core divide+extract
   sequence is emitted up to 4 times per signed DIV/MOD IC; correctness
   over code size for now (see the STATUS comment's remaining-gaps
   list once this task is closed out).

   For UNSIGNED operands, none of this applies (interpreting a value
   with the high bit set as "negative" would corrupt an unsigned
   divide), so DIV/MOD dispatches to a much shorter unsigned-only path
   below instead. */

/* Word case, unsigned-magnitude tail: assumes DE already holds the
   dividend's low 16 bits (zero-extended in the unsigned path, or an
   absolute value in the signed path) and HL already holds the
   divisor. neg_quot/neg_rem are compile-time-literal (0 in the
   unsigned path; a specific branch's fixed combination in the signed
   path). Leaves the wanted result (quotient if !ismod, remainder if
   ismod) in AX and stores it to z. */
static void divmod_word_tail(FILE *f,int neg_quot,int neg_rem,int ismod,struct obj *z,int t)
{
  emit(f,"\tmovw\tax,#0\n");
  emit(f,"\tdivux\thl\n");
  /* |q1|<2^16 and the divisor is nonzero (or hardware's defined
     div-by-zero behavior applies) guarantees the quotient fits in 16
     bits, so AX is back to 0 here -- DE holds the quotient, HL the
     remainder. */
  if(ismod){
    if(neg_rem){ emit(f,"\tmovw\tax,#0\n"); emit(f,"\tsubw\tax,hl\n"); }
    else       { emit(f,"\tmovw\tax,hl\n"); }
  }else{
    if(neg_quot){ emit(f,"\tmovw\tax,#0\n"); emit(f,"\tsubw\tax,de\n"); }
    else        { emit(f,"\tmovw\tax,de\n"); }
  }
  store_word(f,REG_AX,z,t); /* AX is always r1-eligible -- see store_word()'s contract */
}

/* Byte case, unsigned-magnitude tail: assumes A already holds #0 and X
   already holds the dividend (zero-extended/absolute-valued), and C
   already holds the divisor. Leaves the wanted result in A and stores
   it to z. */
static void divmod_byte_tail(FILE *f,int neg_quot,int neg_rem,int ismod,struct obj *z,int t)
{
  emit(f,"\tdivuw\tc\n");
  /* dividend<256 guarantees the quotient fits in a byte, landing
     entirely in X (AX's low half) with A back to 0; C holds the
     remainder. */
  if(ismod){
    if(neg_rem){ emit(f,"\tmov\ta,#0\n"); emit(f,"\tsub\ta,c\n"); }
    else       { emit(f,"\tmov\ta,c\n"); }
  }else{
    if(neg_quot){ emit(f,"\tmov\ta,#0\n"); emit(f,"\tsub\ta,x\n"); }
    else        { emit(f,"\tmov\ta,x\n"); }
  }
  store_byte(f,REG_A,z,t);
}

/* Full signed word DIV/MOD: 4-way sign branch feeding divmod_word_tail(). */
static void gen_signed_word_divmod(FILE *f,struct IC *p,int t,int ismod)
{
  int lab_q1neg=++label, lab_q2negA=++label, lab_q2negB=++label, lab_end=++label;

  load_word(f,REG_AX,&p->q1,t);
  emit(f,"\tcmpw\tax,#0\n");
  emit(f,"\tblt\t%s%d\n",labprefix,lab_q1neg);

  /* q1 >= 0: |q1| = q1 (currently in ax) -- stash in bc, out of the
     way of q2's own load/sign-test, which reuses ax/hl. */
  emit(f,"\tmovw\tbc,ax\n");
  load_word(f,REG_HL,&p->q2,t);
  emit(f,"\tmovw\tax,hl\n");
  emit(f,"\tcmpw\tax,#0\n");
  emit(f,"\tblt\t%s%d\n",labprefix,lab_q2negA);
  /* q1>=0, q2>=0: neither operand negated; quotient positive, dividend non-negative so remainder non-negative. */
  emit(f,"\tmovw\tde,bc\n"); /* dividend, relocated into DE LAST -- see resolve_pointer()'s ordering discipline */
  divmod_word_tail(f,0,0,ismod,&p->z,t);
  emit(f,"\tbr\t%s%d\n",labprefix,lab_end);
  emit(f,"%s%d:\n",labprefix,lab_q2negA);
  /* q1>=0, q2<0: negate hl (still holding q2's raw value) into |q2|. */
  emit(f,"\tmovw\tax,#0\n");
  emit(f,"\tsubw\tax,hl\n");
  emit(f,"\tmovw\thl,ax\n");
  emit(f,"\tmovw\tde,bc\n");
  divmod_word_tail(f,1,0,ismod,&p->z,t);
  emit(f,"\tbr\t%s%d\n",labprefix,lab_end);

  emit(f,"%s%d:\n",labprefix,lab_q1neg);
  /* q1 < 0: negate ax (still holding q1's raw value) into |q1|, using
     hl as scratch for the raw value (safe -- q2 hasn't been loaded
     yet). */
  emit(f,"\tmovw\thl,ax\n");
  emit(f,"\tmovw\tax,#0\n");
  emit(f,"\tsubw\tax,hl\n");
  emit(f,"\tmovw\tbc,ax\n");
  load_word(f,REG_HL,&p->q2,t);
  emit(f,"\tmovw\tax,hl\n");
  emit(f,"\tcmpw\tax,#0\n");
  emit(f,"\tblt\t%s%d\n",labprefix,lab_q2negB);
  /* q1<0, q2>=0: signs differ (quotient negative); dividend was negative (remainder negative). */
  emit(f,"\tmovw\tde,bc\n");
  divmod_word_tail(f,1,1,ismod,&p->z,t);
  emit(f,"\tbr\t%s%d\n",labprefix,lab_end);
  emit(f,"%s%d:\n",labprefix,lab_q2negB);
  /* q1<0, q2<0: signs match (quotient positive); dividend was negative (remainder negative). */
  emit(f,"\tmovw\tax,#0\n");
  emit(f,"\tsubw\tax,hl\n");
  emit(f,"\tmovw\thl,ax\n");
  emit(f,"\tmovw\tde,bc\n");
  divmod_word_tail(f,0,1,ismod,&p->z,t);

  emit(f,"%s%d:\n",labprefix,lab_end);
}

/* Full signed byte DIV/MOD: same 4-way structure as the word case
   above, using DIVUW (word AX / byte r1) instead of DIVUX, with the
   dividend zero-extended into AX (A=0,X=|q1|) rather than spanning a
   32-bit pair. Every register read here is x/a/c/b -- never d/e/h/l --
   so none of this is subject to the r1-source restriction. */
static void gen_signed_byte_divmod(FILE *f,struct IC *p,int t,int ismod)
{
  int lab_q1neg=++label, lab_q2negA=++label, lab_q2negB=++label, lab_end=++label;

  load_byte(f,REG_A,&p->q1,t);
  emit(f,"\tcmp\ta,#0\n");
  emit(f,"\tblt\t%s%d\n",labprefix,lab_q1neg);

  /* q1 >= 0: |q1| = q1 (currently in a) -- stash in b. */
  emit(f,"\tmov\tb,a\n");
  load_byte(f,REG_C,&p->q2,t);
  emit(f,"\tmov\ta,c\n");
  emit(f,"\tcmp\ta,#0\n");
  emit(f,"\tblt\t%s%d\n",labprefix,lab_q2negA);
  /* q1>=0, q2>=0 */
  emit(f,"\tmov\tx,b\n");
  emit(f,"\tmov\ta,#0\n");
  divmod_byte_tail(f,0,0,ismod,&p->z,t);
  emit(f,"\tbr\t%s%d\n",labprefix,lab_end);
  emit(f,"%s%d:\n",labprefix,lab_q2negA);
  /* q1>=0, q2<0: negate c (still holding q2's raw value) into |q2|. */
  emit(f,"\tmov\ta,#0\n");
  emit(f,"\tsub\ta,c\n");
  emit(f,"\tmov\tc,a\n");
  emit(f,"\tmov\tx,b\n");
  emit(f,"\tmov\ta,#0\n");
  divmod_byte_tail(f,1,0,ismod,&p->z,t);
  emit(f,"\tbr\t%s%d\n",labprefix,lab_end);

  emit(f,"%s%d:\n",labprefix,lab_q1neg);
  /* q1 < 0: negate a (still holding q1's raw value) into |q1|, via b
     as scratch for the raw value. */
  emit(f,"\tmov\tb,a\n");
  emit(f,"\tmov\ta,#0\n");
  emit(f,"\tsub\ta,b\n");
  emit(f,"\tmov\tb,a\n");
  load_byte(f,REG_C,&p->q2,t);
  emit(f,"\tmov\ta,c\n");
  emit(f,"\tcmp\ta,#0\n");
  emit(f,"\tblt\t%s%d\n",labprefix,lab_q2negB);
  /* q1<0, q2>=0 */
  emit(f,"\tmov\tx,b\n");
  emit(f,"\tmov\ta,#0\n");
  divmod_byte_tail(f,1,1,ismod,&p->z,t);
  emit(f,"\tbr\t%s%d\n",labprefix,lab_end);
  emit(f,"%s%d:\n",labprefix,lab_q2negB);
  /* q1<0, q2<0: negate c into |q2|. */
  emit(f,"\tmov\ta,#0\n");
  emit(f,"\tsub\ta,c\n");
  emit(f,"\tmov\tc,a\n");
  emit(f,"\tmov\tx,b\n");
  emit(f,"\tmov\ta,#0\n");
  divmod_byte_tail(f,0,1,ismod,&p->z,t);

  emit(f,"%s%d:\n",labprefix,lab_end);
}

/* ------------------------------------------------------------------ */
/* LSHIFT/RSHIFT helpers.

   Hardware has no shift-by-register form at all -- SHR/SHL/SHRW/SHLW's
   count is always a compile-time 3-bit immediate (0-7), never a
   register (confirmed against the real instruction manual) -- so only
   a KONST q2 (shift count known at compile time) can be handled here;
   a runtime-variable count is a real, documented gap (see the STATUS
   comment). There is also no arithmetic (sign-preserving) right shift
   in hardware, only logical SHR/SHRW (zero-fill) -- so a signed
   RSHIFT needs a manual sign-extension fixup: shift logically, then
   if the original value was negative, OR in 1s over the bits the
   shift vacated. Since the fixup mask is a compile-time constant
   (cnt is KONST) this needs no runtime mask computation, just a
   runtime branch on q1's sign to decide whether to apply it.

   A word shift by 8-15 has no single hardware instruction (the count
   field cannot reach 8), so it's split into a whole-byte relocation
   (an 8-bit shift is just "move to the other byte, zero/sign-fill the
   vacated one") plus a residual shift of the remainder (0-7) -- see
   gen_word_shift()'s cnt>=8 branch. */

/* Byte case. cnt is already known non-negative (see the KONST check
   at the call site); values >=8 are UB in C (shifting a byte-width
   type by its own width) but are still given a sane, fully-shifted-out
   result rather than emitting nonsense. */
static void gen_byte_shift(FILE *f,struct IC *p,int t,int rshift,int uns,long cnt)
{
  if(cnt==0){
    load_byte(f,REG_A,&p->q1,t);
    store_byte(f,REG_A,&p->z,t);
    return;
  }
  if(cnt>=8){
    if(rshift&&!uns){
      int lneg=++label,ldone=++label;
      load_byte(f,REG_A,&p->q1,t);
      emit(f,"\tcmp\ta,#0\n");
      emit(f,"\tblt\t%s%d\n",labprefix,lneg);
      emit(f,"\tmov\ta,#0\n");
      emit(f,"\tbr\t%s%d\n",labprefix,ldone);
      emit(f,"%s%d:\n",labprefix,lneg);
      emit(f,"\tmov\ta,#0xff\n");
      emit(f,"%s%d:\n",labprefix,ldone);
    }else{
      emit(f,"\tmov\ta,#0\n");
    }
    store_byte(f,REG_A,&p->z,t);
    return;
  }
  /* 1 <= cnt <= 7 */
  load_byte(f,REG_A,&p->q1,t);
  if(!rshift){
    emit(f,"\tshl\ta,%ld\n",cnt);
  }else if(uns){
    emit(f,"\tshr\ta,%ld\n",cnt);
  }else{
    int lneg=++label,ldone=++label;
    long mask=(0xFF<<(8-cnt))&0xFF; /* top 'cnt' bits of a byte */
    emit(f,"\tcmp\ta,#0\n");
    emit(f,"\tblt\t%s%d\n",labprefix,lneg);
    emit(f,"\tshr\ta,%ld\n",cnt);
    emit(f,"\tbr\t%s%d\n",labprefix,ldone);
    emit(f,"%s%d:\n",labprefix,lneg);
    emit(f,"\tshr\ta,%ld\n",cnt);
    emit(f,"\tor\ta,#%ld\n",mask);
    emit(f,"%s%d:\n",labprefix,ldone);
  }
  store_byte(f,REG_A,&p->z,t);
}

/* Word case. Same structure as gen_byte_shift(), plus the 8-15 byte-
   relocation split described in the block comment above. */
static void gen_word_shift(FILE *f,struct IC *p,int t,int rshift,int uns,long cnt)
{
  if(cnt==0){
    load_word(f,REG_AX,&p->q1,t);
    store_word(f,REG_AX,&p->z,t);
    return;
  }
  if(cnt>=16){
    if(rshift&&!uns){
      int lneg=++label,ldone=++label;
      load_word(f,REG_AX,&p->q1,t);
      emit(f,"\tcmp\ta,#0\n"); /* a is ax's high byte -- carries q1's sign */
      emit(f,"\tblt\t%s%d\n",labprefix,lneg);
      emit(f,"\tmovw\tax,#0\n");
      emit(f,"\tbr\t%s%d\n",labprefix,ldone);
      emit(f,"%s%d:\n",labprefix,lneg);
      emit(f,"\tmovw\tax,#-1\n");
      emit(f,"%s%d:\n",labprefix,ldone);
    }else{
      emit(f,"\tmovw\tax,#0\n");
    }
    store_word(f,REG_AX,&p->z,t);
    return;
  }
  if(cnt<=7){
    load_word(f,REG_AX,&p->q1,t);
    if(!rshift){
      emit(f,"\tshlw\tax,%ld\n",cnt);
    }else if(uns){
      emit(f,"\tshrw\tax,%ld\n",cnt);
    }else{
      int lneg=++label,ldone=++label;
      long mask=(0xFF<<(8-cnt))&0xFF; /* cnt<=7 -- entirely within the high byte */
      emit(f,"\tcmp\ta,#0\n");
      emit(f,"\tblt\t%s%d\n",labprefix,lneg);
      emit(f,"\tshrw\tax,%ld\n",cnt);
      emit(f,"\tbr\t%s%d\n",labprefix,ldone);
      emit(f,"%s%d:\n",labprefix,lneg);
      emit(f,"\tshrw\tax,%ld\n",cnt);
      emit(f,"\tor\ta,#%ld\n",mask); /* orw doesn't exist -- only the high byte needs the fixup here */
      emit(f,"%s%d:\n",labprefix,ldone);
    }
    store_word(f,REG_AX,&p->z,t);
    return;
  }
  /* 8 <= cnt <= 15 */
  {
    long k=cnt-8;
    load_word(f,REG_AX,&p->q1,t);
    if(!rshift){
      /* new_high = old_low << k (bits shifted past bit 7 are simply
         discarded, correctly matching a 16-bit left shift by 8+k);
         new_low = 0. */
      emit(f,"\tmov\ta,x\n");
      if(k>0) emit(f,"\tshl\ta,%ld\n",k);
      emit(f,"\tmov\tx,#0\n");
    }else if(uns){
      /* new_low = old_high >> k (logical); new_high = 0. */
      emit(f,"\tmov\tx,a\n");
      if(k>0) emit(f,"\tshr\tx,%ld\n",k);
      emit(f,"\tmov\ta,#0\n");
    }else{
      /* new_low = old_high >> k (logical), 1-filling the vacated top
         k bits if q1 was negative (q1's sign lives entirely in
         old_high, which becomes the new low byte here); new_high =
         all sign bits. or's immediate form only exists for a (see its
         opcode-table entry), so the fixup runs through a and is
         copied into x afterward rather than operating on x directly. */
      int lneg=++label,ldone=++label;
      long mask=k>0?((0xFF<<(8-k))&0xFF):0;
      emit(f,"\tmov\tx,a\n");  /* x = old_high, candidate new low byte */
      emit(f,"\tcmp\ta,#0\n"); /* a still = old_high (unmodified) -- test its sign */
      emit(f,"\tblt\t%s%d\n",labprefix,lneg);
      if(k>0) emit(f,"\tshr\tx,%ld\n",k);
      emit(f,"\tmov\ta,#0\n");
      emit(f,"\tbr\t%s%d\n",labprefix,ldone);
      emit(f,"%s%d:\n",labprefix,lneg);
      if(k>0) emit(f,"\tshr\tx,%ld\n",k);
      emit(f,"\tmov\ta,x\n");
      emit(f,"\tor\ta,#%ld\n",mask);
      emit(f,"\tmov\tx,a\n");
      emit(f,"\tmov\ta,#0xff\n");
      emit(f,"%s%d:\n",labprefix,ldone);
    }
    store_word(f,REG_AX,&p->z,t);
  }
}

/* ------------------------------------------------------------------ */
/* Runtime-variable (non-KONST) shift count -- task #24.

   Hardware truly has no shift-by-register form at all (see the block
   comment above gen_byte_shift()), so a variable count is done as a
   software loop: shift by 1, N times, decrementing a runtime counter.
   DBNZ ("decrement register, branch if the result is nonzero") is the
   natural loop primitive for this -- confirmed as a real, already-
   used-in-the-actual-firmware instruction (12 real instances in the
   mask ROM disassembly, e.g. "dbnz c,0x0b17"), not just present in
   shared/Opcodes.h's table. Its counter operand is restricted to C or
   B only (OT_R2's 1-bit register field -- see shared/RegisterNames.h)
   -- C is used throughout below, matching the existing convention
   elsewhere in this file of using C as the "other" byte scratch.

   Guarding against a zero count: DBNZ decrements FIRST and branches
   if the result is nonzero, i.e. it's a natural "loop exactly N-1
   more times after this one" primitive, correct only once you already
   know N>=1 -- called on a register that's already 0, it would
   decrement to 0xff (255) and loop 255 times, corrupting the result.
   So every path below tests C for zero BEFORE entering the DBNZ loop,
   skipping it entirely when the runtime count is 0 -- via "mov a,c /
   cmp a,#0 / be lzero", not the more obvious "cmp c,#0" directly,
   because CMP's immediate form only ever pairs with A (confirmed
   empirically: vasm rejects "cmp c,#0" outright, and shared/
   Opcodes.h's M_CMP rows never pair OP_BYTE with OP_R1/OP_R2). Using
   A as scratch here, before q1 has been loaded into it, is why q1's
   own load is duplicated on the "count is zero" path below (see
   gen_byte_shift_var()/gen_word_shift_var() themselves) rather than
   hoisted above this test the way the byte/word non-loop paths above
   do it.

   Signed RSHIFT's sign-extension fixup (hardware's SHR/SHRW are
   logical/zero-fill only, same gap as the KONST-count path above) is
   applied PER SINGLE-BIT ITERATION here rather than once with a
   compile-time mask, since the shift amount isn't known until
   runtime: the operand's sign is invariant across every iteration of
   an arithmetic right shift (that's the definition of sign
   extension), so it's tested exactly ONCE, before the loop, to choose
   between two loop bodies (one that leaves the top bit 0 each step,
   one that forces it to 1 each step) -- not re-tested every
   iteration, which would be both slower and wrong after the first
   iteration already started overwriting the original sign bit.

   A count of 8+ (byte) or 16+ (word), or any count large enough that
   every value bit has already been shifted out, needs no special
   case the way the KONST path's cnt>=8/cnt>=16 branches special-case
   it: single-bit-at-a-time shifting is naturally idempotent once the
   register is fully zero (logical shifts, unsigned arithmetic) or
   fully sign-filled (signed arithmetic) -- further iterations just
   keep re-shifting an already-saturated value into itself. So a
   plain "loop exactly count times" is correct for any nonnegative
   count, no separate large-count fast path needed for correctness
   (only for speed, not pursued here).

   The runtime count (q2) is loaded into the loop counter BEFORE q1 is
   loaded into the working register (A or AX) -- the same "load the
   VAR/DREFOBJ-class operand into its non-working-register pair FIRST"
   discipline load_word()'s own hazard comment documents (every memory
   load bounces through register A regardless of destination), applied
   here with C standing in for the usual HL/BC "other operand" slot.
   q2's own type is p->typf2 (q2typ(p), NOT t -- see supp.h's q2typ()
   macro), independent of q1/z's type t: a runtime shift count can be
   char- or int-typed regardless of what's being shifted, per
   interface.texi ("All operands are integers", no width tied to q1).
   When q2 is word-typed, only its low byte matters as a loop counter
   (see the paragraph above on large counts already saturating
   correctly) -- so it's loaded into BC and only C is used, discarding
   B; this can only under-count a TRUE runtime value if that value is
   a multiple of 256, which is already shift-by->=width UB in C, same
   look-the-other-way latitude the KONST path takes for an
   out-of-range compile-time count.

   VERIFICATION NOTE (directly relevant after task #24's methodology
   finding above): these loops are branch-heavy, so they were checked
   through the CORRECTED assemble+link pipeline (vasm -Fvobj + vlink
   -Ttext 0 -b rawbin1, not raw vasm -Fbin) and hand-disassembled to
   confirm every DBNZ's and BLT's displacement byte is a real, nonzero
   value matching its true target -- not just re-run through the
   symbolic simulator, which would not have caught a repeat of that
   bug since it never depended on real encoding in the first place. */
static void gen_byte_shift_var(FILE *f,struct IC *p,int t,int rshift,int uns,int q2word)
{
  int ldone,lzero,lloop,lneg;
  if(q2word){
    load_word(f,REG_BC,&p->q2,q2typ(p));
  }else{
    load_byte(f,REG_C,&p->q2,q2typ(p));
  }
  /* Test the runtime count for zero. CMP's immediate form is A-only --
     confirmed empirically (vasm rejects "cmp c,#0" with "illegal
     operand types") and by shared/Opcodes.h's M_CMP rows, which only
     ever pair an immediate BYTE operand with OP_A, never OP_R1/OP_R2.
     So the test has to go through A -- but A isn't free yet (q1 hasn't
     been loaded), so this uses A as pure scratch here and q1 gets
     loaded AFTER, on each of the two paths below (the lzero path and
     the main body) -- a small deliberate duplication rather than
     clobbering q1 by testing after loading it. */
  emit(f,"\tmov\ta,c\n");
  emit(f,"\tcmp\ta,#0\n");
  lzero=++label;
  ldone=++label;
  emit(f,"\tbe\t%s%d\n",labprefix,lzero);
  load_byte(f,REG_A,&p->q1,t);
  if(!rshift){
    lloop=++label;
    emit(f,"%s%d:\n",labprefix,lloop);
    emit(f,"\tshl\ta,1\n");
    emit(f,"\tdbnz\tc,%s%d\n",labprefix,lloop);
  }else if(uns){
    lloop=++label;
    emit(f,"%s%d:\n",labprefix,lloop);
    emit(f,"\tshr\ta,1\n");
    emit(f,"\tdbnz\tc,%s%d\n",labprefix,lloop);
  }else{
    lneg=++label;
    emit(f,"\tcmp\ta,#0\n");
    emit(f,"\tblt\t%s%d\n",labprefix,lneg);
    lloop=++label;
    emit(f,"%s%d:\n",labprefix,lloop);
    emit(f,"\tshr\ta,1\n");
    emit(f,"\tdbnz\tc,%s%d\n",labprefix,lloop);
    emit(f,"\tbr\t%s%d\n",labprefix,ldone);
    emit(f,"%s%d:\n",labprefix,lneg);
    lloop=++label;
    emit(f,"%s%d:\n",labprefix,lloop);
    emit(f,"\tshr\ta,1\n");
    emit(f,"\tor\ta,#0x80\n");
    emit(f,"\tdbnz\tc,%s%d\n",labprefix,lloop);
  }
  emit(f,"\tbr\t%s%d\n",labprefix,ldone);
  emit(f,"%s%d:\n",labprefix,lzero);
  load_byte(f,REG_A,&p->q1,t);
  emit(f,"%s%d:\n",labprefix,ldone);
  store_byte(f,REG_A,&p->z,t);
}

/* Word case -- same structure, SHLW/SHRW ax,1 in place of SHL/SHR a,1;
   the sign test uses A (ax's high byte) same as gen_word_shift()'s
   KONST-count path does. Same A-is-the-only-CMP-immediate-register
   constraint applies to the zero-count test, and the same fix (test
   before loading q1, duplicate the load on the zero path). */
static void gen_word_shift_var(FILE *f,struct IC *p,int t,int rshift,int uns,int q2word)
{
  int ldone,lzero,lloop,lneg;
  if(q2word){
    load_word(f,REG_BC,&p->q2,q2typ(p));
  }else{
    load_byte(f,REG_C,&p->q2,q2typ(p));
  }
  emit(f,"\tmov\ta,c\n");
  emit(f,"\tcmp\ta,#0\n");
  lzero=++label;
  ldone=++label;
  emit(f,"\tbe\t%s%d\n",labprefix,lzero);
  load_word(f,REG_AX,&p->q1,t);
  if(!rshift){
    lloop=++label;
    emit(f,"%s%d:\n",labprefix,lloop);
    emit(f,"\tshlw\tax,1\n");
    emit(f,"\tdbnz\tc,%s%d\n",labprefix,lloop);
  }else if(uns){
    lloop=++label;
    emit(f,"%s%d:\n",labprefix,lloop);
    emit(f,"\tshrw\tax,1\n");
    emit(f,"\tdbnz\tc,%s%d\n",labprefix,lloop);
  }else{
    lneg=++label;
    emit(f,"\tcmp\ta,#0\n");
    emit(f,"\tblt\t%s%d\n",labprefix,lneg);
    lloop=++label;
    emit(f,"%s%d:\n",labprefix,lloop);
    emit(f,"\tshrw\tax,1\n");
    emit(f,"\tdbnz\tc,%s%d\n",labprefix,lloop);
    emit(f,"\tbr\t%s%d\n",labprefix,ldone);
    emit(f,"%s%d:\n",labprefix,lneg);
    lloop=++label;
    emit(f,"%s%d:\n",labprefix,lloop);
    emit(f,"\tshrw\tax,1\n");
    emit(f,"\tor\ta,#0x80\n");
    emit(f,"\tdbnz\tc,%s%d\n",labprefix,lloop);
  }
  emit(f,"\tbr\t%s%d\n",labprefix,ldone);
  emit(f,"%s%d:\n",labprefix,lzero);
  load_word(f,REG_AX,&p->q1,t);
  emit(f,"%s%d:\n",labprefix,ldone);
  store_word(f,REG_AX,&p->z,t);
}

/* Unsigned word/byte DIV/MOD: no sign correction at all, just a
   straight zero-extended divide -- much shorter than the signed path. */
static void gen_unsigned_word_divmod(FILE *f,struct IC *p,int t,int ismod)
{
  load_word(f,REG_HL,&p->q2,t);
  load_word(f,REG_BC,&p->q1,t); /* dividend, staged in BC first (load_word() refuses DE directly -- see its own comment) */
  emit(f,"\tmovw\tde,bc\n");
  divmod_word_tail(f,0,0,ismod,&p->z,t);
}
static void gen_unsigned_byte_divmod(FILE *f,struct IC *p,int t,int ismod)
{
  load_byte(f,REG_C,&p->q2,t);
  load_byte(f,REG_X,&p->q1,t); /* non-A destination first -- see load_byte()'s bounce-through-A hazard comment */
  emit(f,"\tmov\ta,#0\n");
  divmod_byte_tail(f,0,0,ismod,&p->z,t);
}

/* Word-sized bitwise AND/OR/XOR: no hardware ANDW/ORW/XORW exists (see
   shared/Mnemonic.h -- confirmed by a full-file dump, same absence
   already noted for MULW). Unlike ADD/SUB, a bitwise op has zero
   inter-byte dependency (no carry/borrow crosses the byte boundary),
   so it decomposes trivially into two fully independent byte ops --
   unlike DIV/MOD or a multi-bit shift, there is no sign correction or
   cross-byte fixup to reason about at all.

   Register plan: load q2 fully into BC FIRST (b=q2_hi, c=q2_lo) --
   this mirrors load_word()'s own load-before-q1 discipline and is
   safe even if q2 is a dereferenced pointer (resolve_pointer()'s DE
   scratch is fully consumed by the time load_word() returns). THEN
   load q1 into AX (a=q1_hi, x=q1_lo) -- safe for the same reason if
   q1 is ALSO a dereferenced pointer, since q2's value is by then
   sitting safely in BC, independent of DE.

   Both result bytes are computed through A one at a time (every
   AND/OR/XOR's register-register form is A-only on the destination
   side, and B/C are r1-eligible sources -- see byte_reg_ok()), and
   stashed into H and L respectively (H/L are otherwise unused here,
   and are NOT r1-eligible, but nothing ever needs to read a value
   BACK OUT of them individually -- they're only ever reassembled as
   the whole PAIR via a single "movw ax,hl", which is exempt from the
   r1-source restriction, exactly like MULT's HL->AX relocation
   above). store_word() then requires exactly this AX placement (it
   asserts against HL/DE -- see its own comment). */
static void gen_word_bitwise(FILE *f,struct IC *p,int t,const char *mn)
{
  load_word(f,REG_BC,&p->q2,t);
  load_word(f,REG_AX,&p->q1,t);
  emit(f,"\t%s\ta,b\n",mn);
  emit(f,"\tmov\th,a\n");
  emit(f,"\tmov\ta,x\n");
  emit(f,"\t%s\ta,c\n",mn);
  emit(f,"\tmov\tl,a\n");
  emit(f,"\tmovw\tax,hl\n");
  store_word(f,REG_AX,&p->z,t);
}

/* ------------------------------------------------------------------ */
/* gen_code(): scalar (char/short/int/pointer) arithmetic, comparison,
   control flow, return-value/register-move ICs, CALL/PUSH, and now
   pointer dereference (DREFOBJ) -- see resolve_pointer() and the
   DREFOBJ branches load_byte()/store_byte()/load_word()/store_word()/
   emit_var_mem() gained: a DREFOBJ operand is resolved into the fixed
   DE scratch pair and accessed via real [de+byte] based addressing
   (shared/Opcodes.h's OP_MEM_MOD, confirmed to accept ANY valid
   [reg+byte] base -- not just SP -- on MOV/ADD/SUB/AND/OR/XOR/CMP).
   Since every IC case already routes its operand access through those
   four helpers (or, for a direct memory operand in a byte ALU/COMPARE
   op, through emit_var_mem()), DREFOBJ support falls out of THOSE
   fixes for every case here for free -- no case below needed its own
   special DREFOBJ branch, except broadening 4 dispatch conditions
   that previously tested `p->q2.flags&VAR` to also accept `&DREFOBJ`
   (a dereferenced q2 no longer carries the VAR bit once resolved), and
   CALL's own q1 check, which now explicitly rejects a DREFOBJ q1 --
   an indirect call through a function pointer needs a "call [reg]"-
   style form we have not implemented, a distinct and separate gap from
   ordinary data-pointer dereference.

   MULT/DIV/MOD/LSHIFT/RSHIFT are now implemented too (see the block
   comments above MULT's case and above divmod_word_tail()/
   gen_byte_shift()/gen_word_shift() for the design). Briefly:
    - MULT reuses the unsigned MULU/MULUW directly for signed multiply
      too, since a two's-complement product's low N bits are the same
      whether the operands are taken as signed or unsigned, and C's
      a*b truncates to the result's own width anyway. No signed
      multiply instruction exists in hardware at all (MULW is in the
      manual's instruction list but was apparently never wired into
      shared/Mnemonic.h/Opcodes.h), but this makes that moot.
    - DIV/MOD have no such shortcut -- hardware divide (DIVUW/DIVUX)
      is genuinely unsigned-only, so a signed divide/modulo computes
      the unsigned quotient/remainder of the operands' absolute values
      via a 4-way compile-time sign branch (q1 sign, then q2 sign),
      then negates the quotient if the signs differed and the
      remainder if the dividend was negative (C truncates toward
      zero). UNSIGNED operands skip all of this and hit hardware
      divide directly. Verified by actually EXECUTING the generated
      assembly (at the time there was no emulator, so a small, scoped
      instruction-level simulator was written for it and not kept;
      the NEC_78KIII_Emulator repository does this job now) -- covering
      every sign combination,
      exact division, dividend 0, divisor +-1, and INT_MIN/CHAR_MIN
      edge cases, byte and word, signed and unsigned).
    - LSHIFT/RSHIFT only support a KONST (compile-time-constant) shift
      count -- hardware has no shift-by-register form at all. A signed
      RSHIFT needs a manual sign-extension fixup after the hardware's
      logical-only SHR/SHRW (branch on q1's sign, OR in 1s over the
      vacated top bits when negative -- the mask is a compile-time
      constant since the count is KONST). A word shift by 8-15 has no
      single instruction (the hardware count field is 3 bits, max 7),
      so it's split into a whole-byte relocation plus a residual
      shift under 8. Also verified via the same execution-based
      testing (counts 0, 1-7, 8, exactly-8-boundary, 12, 15, and >=16,
      both directions, signed/unsigned, byte/word).

   Word AND/OR/XOR, TEST, KOMPLEMENT, and pointer arithmetic (ADDI2P/
   SUBIFP/SUBPFP -- array indexing, p+i/p-i/p++, pointer difference)
   are now implemented too -- see gen_word_bitwise()'s comment, the
   TEST/KOMPLEMENT cases' own comments, and the ADDI2P/SUBIFP/SUBPFP
   cases just above ADDRESS. Briefly:
    - Word bitwise ops decompose into two independent byte ops (no
      inter-byte dependency, unlike ADD/SUB's carry/borrow).
    - TEST is just COMPARE q1,#0 (our CMP sets all flags for real, a
      superset of the spec's "only BEQ/BNE required").
    - KOMPLEMENT is x^ALL_ONES (XOR was already available).
    - ADDI2P/SUBIFP's q2 is already the byte offset (element-size
      scaling happens upstream, confirmed by MINADDI2P/MAXADDI2P both
      being INT in machine.h) and SUBPFP's result is already in bytes
      (no upstream element-size division either) -- so all three are
      nothing more than a plain word add/sub.

   *** A real, previously-latent, SILENT MISCOMPILATION bug was found
   and fixed while implementing ADDI2P (via t18.c's idx_int(), array
   indexing with a runtime-variable index) and traced back to COMPARE
   and ADD/SUB's word cases as well, which had shipped with it since
   tasks #12/#15: load_word()'s VAR/DREFOBJ branches always bounce
   through register A internally, *regardless of destination pair* --
   so the established call-site idiom "load_word(AX,q1) first, THEN
   load_word(HL,q2) if q2 is VAR/DREFOBJ" silently clobbered q1's high
   byte (register A, part of AX) with q2's high byte before the addw/
   subw/cmpw ever ran, whenever q2 needed that path. The corruption is
   MASKED (produces the right answer by coincidence) whenever q1 and
   q2 happen to share the same high byte -- e.g. any two ints under
   256 -- which is almost certainly why it went uncaught across every
   earlier task's test files despite affecting the single most common
   operation in the language (adding/comparing two runtime int values).
   Confirmed and quantified by literally re-simulating the OLD, still-
   on-disk .s output for t1.c's add3() and t7.c's add_through_ptr()/
   compare_two_ptrs() against the NEW output: e.g. add3(1000,2000,3)
   computed 187 instead of 3003 under the old code, and 3003 (correct)
   after the fix -- same for a DREFOBJ q2 (add_through_ptr) and for
   COMPARE (compare_two_ptrs(50,1280) returned "greater" when it should
   not have). Fixed by moving each affected call site's load_word(AX,
   q1,...) to happen PER-BRANCH, strictly AFTER load_word(HL,q2,...)
   in the VAR/DREFOBJ branch specifically (mirroring MULT's and
   gen_word_bitwise()'s already-safe "q2 first, q1 right before it's
   needed" ordering) -- see COMPARE's, ADD/SUB's, and ADDI2P/SUBIFP/
   SUBPFP's own comments for the exact reasoning. load_word() itself
   was deliberately left unchanged (it is correct and heavily used
   elsewhere; only the two-operands-live-in-AX-then-HL call pattern
   was the hazard), to keep the fix small and not risk the many
   already-verified passing cases. DIV/MOD's word paths were already
   safe (they explicitly relocate q1 out of AX into BC via a whole-
   pair movw before ever loading q2) and needed no change.

   Remaining gaps, still calling unsupported_ic() rather than
   guessing: struct-by-value function arguments in PUSH (task #25).
   Indirect calls through a function pointer (task #23) and a
   runtime-variable (non-KONST) shift count (task #24, see
   gen_byte_shift_var()/gen_word_shift_var() below) are now handled.

   *** CRITICAL METHODOLOGY FINDING (task #24): "vasm -Fbin" alone,
   our verification pipeline's assemble step for EVERY test file so
   far, silently mis-encodes EVERY symbolic branch (br/bne/blt/.../
   dbnz to a label, forward OR backward) in any file that starts with
   an ordinary ".text" section -- not a 78k3-specific bug, a property
   of vasm's generic core (expr.c's eval_expr() SYM case: a label's
   address is only treated as a compile-time CONSTANT if its symbol
   carries ABSLABEL, which symbol.c's new_labsym() only sets when the
   label's section has the ABSOLUTE flag -- an ordinary ".text"
   section never does, by design, since in the general case a
   relocatable section's real address isn't known until a linker
   places it). Without ABSLABEL, cpu.c's encode_operand() can't fold
   the branch target to a number, so it falls back to emitting an
   external relocation for vlink to patch later -- and output_bin.c
   (confirmed by inspection: it contains no relocation-handling code
   at all) NEVER applies it, silently leaving the displacement BYTE
   AT ZERO. The generated assembly still LOOKS right, vasm still
   exits 0, and our own simulator (which interprets the .s TEXT
   directly via a label dictionary, never touching vasm's real
   encoding at all) still executes it correctly -- so this went
   completely undetected until this task, caught only by chance while
   sanity-checking DBNZ's encoding by hand-disassembling a real
   assembled+linked binary before trusting it for the shift-count
   loop below. Confirmed via a temporary debug patch to cpu.c's
   encode_operand() (added, used, then fully reverted -- diffed
   against a saved backup to confirm cpu.c is byte-identical to
   before this investigation) printing eval_const()'s result for
   every OT_ADDR16R operand: false, every time, for a plain ".text"
   file, true for every branch in the *same-shaped* code when the
   section is instead created via ".org 0" (which DOES set ABSOLUTE,
   confirmed by tracing vasm.c's new_org()). A disassembled t18.s
   compiled this way showed EVERY branch (forward and backward alike)
   encoded as displacement 0 -- i.e. every single one a no-op that
   just falls into the next instruction -- silently breaking every
   loop and every multi-instruction conditional this backend has ever
   emitted, though never caught by execution testing since the
   simulator sidesteps real encoding entirely.
   The two existing byte-level regression checks are UNAFFECTED and
   remain valid evidence, for different reasons confirmed by
   inspection, not assumed: the mask-ROM round-trip
   (rt_test/full_pipeline.py) feeds vasm literal numeric addresses
   ("br 0x0273"), never symbols, so eval_const() never even reaches
   the SYM case; the opcode sweep (opcode_sweep/sweep.py) never emits
   a ".text" directive at all, so its branch labels live in vasm's
   own implicit default section (created via new_org(), which DOES
   set ABSOLUTE) purely by accident of construction, not by any
   awareness of this issue at the time it was written.
   THE FIX (a testing-methodology fix, not a code change -- nothing
   in machine.c, cpu.c, or vasm/vlink's own logic needed to change,
   and deliberately NOT "make .text sections ABSOLUTE", which would
   wrongly break real linkability for the eventual actual-firmware
   use case): assemble compiler output as a relocatable object and
   let vlink resolve it, instead of asking vasm for a flat binary
   directly -- `vasm78k3_std -Fvobj -o x.o x.s` then
   `vlink -Ttext 0 -b rawbin1 -o x.bin x.o`. Confirmed correct by
   disassembling the result: a minimal backward-branch test and a
   full rebuild of t18.s (real forward AND backward branches, the
   latter from its array-sum loop) both now show correct nonzero
   displacements matching their true targets, verified by hand
   (target - address-after-instruction, matching the disassembler's
   own formula) for every branch instance. Every simulator-verified
   test file (t1-t21) was re-assembled+linked through this corrected
   pipeline and confirmed to link with zero errors. This is now the
   required assemble step for any future BYTE-LEVEL check of
   compiler output (the simulator itself is unaffected and remains
   valid for LOGIC verification, since it never depended on vasm's
   real encoding in the first place). */

/* The value of a KONST operand, as a long. vbcc stores a constant in
   the union member that matches its type (vint for INT, vuchar for
   UNSIGNED CHAR, ...); only that member is written. Reading val.vmax
   instead picks up whatever the rest of the union held -- heap garbage --
   so the result changed with the build directory (the host's heap
   layout). eval_const() reads the right member for the type and leaves
   the value in vmax. */
static long konst_long(struct obj *o,int t)
{
  eval_const(&o->val,t);
  return zm2l(vmax);
}

/* ------------------------------------------------------------------
   Native SET1/CLR1 for byte compound assignments against a single
   constant bit mask (task #46, first pass -- deliberately scoped, per
   explicit review guidance, to exactly two C idioms:

       x |= <single-bit-mask>;      ->  SET1 <addr>,<bit>
       x &= ~<single-bit-mask>;     ->  CLR1 <addr>,<bit>

   and ONLY when x is the same literal-constant saddr/sfr-window
   address on both sides of the compound assignment -- i.e. a
   compile-time-constant pointer dereference (the SFR8()-style macro
   pattern from task #44), never an ordinary VAR global. That is the
   exact same architectural boundary already established for saddr/
   SFR addressing in general (see the block comment above
   SADDR_WINDOW_LO): vasm's OT_N_BIT operand requires the address to
   be a fully-resolved constant at assemble time (shared/Opcodes.h's
   SET1/CLR1 rows only offer OP_SADDR/OP_SFR/OP_A/OP_X/OP_PSWH/
   OP_PSWL memory forms -- no [sp+byte]/[hl] form at all), which only
   a literal constant address can satisfy; const_deref_addr() already
   encodes precisely that requirement, so it is reused unchanged here
   rather than duplicated.

   Volatile-safety (the reviewer's explicit, primary concern for this
   whole feature -- "must not turn one volatile access into multiple
   reads or suppress a required access"): SET1/CLR1 against an sfr/
   saddr operand is ONE instruction performing exactly the same
   access pattern as the three-instruction sequence it replaces here
   -- one read (the existing path's load_byte of q1) and one write
   (the existing path's store_byte of z). The NEC 78K/III User's
   Manual's clock-cycle tables give SET1/CLR1 sfr.bit 13 clocks (vs.
   1-2 for the pure-register CY form), consistent with an internal
   read-modify-write bus cycle and nothing else -- no explicit "read-
   modify-write" prose citation was found by a full-text search of the
   manual, so this is strong circumstantial evidence, not a spec
   citation, but it is the SAME access count (1 read + 1 write) either
   way: this function only ever fires on an IC shape (q1 and z the
   identical object) whose *existing*, already-shipped codegen already
   performs exactly one read and one write, so the substitution cannot
   change the access count regardless of the instruction's internal
   bus behavior. If anything it is strictly safer for a volatile
   hardware register than what it replaces: the read and the write
   happen as one atomic instruction instead of two separate ones with
   a register-only operation in between.

   Deliberately NOT attempted here (out of scope for this pass, per
   explicit review guidance to keep the first pass narrow and not make
   the optimizer "heroic"): the two "if(x & mask)"/"if(!(x & mask))"
   idioms. Those map to BT/BF, not SET1/CLR1, and need a genuinely
   different, multi-IC peephole (AND immediately followed by TEST
   immediately followed by BEQ/BNE, with the AND's result temp proven
   unused anywhere else) rather than this single-IC compound-
   assignment recognition -- a real, independently-tractable follow-on,
   not implemented in this pass; see the status note after this
   function is wired in for the scoping question raised to the user.

   Returns 1 if native code was already emitted (caller must skip the
   generic ADD/SUB/AND/OR/XOR path for this IC) or 0 if not eligible
   (caller falls through to the generic path, unchanged). */
static int try_native_bit_compound_assign(FILE *f,struct IC *p,int c,int t)
{
  unsigned long addr1,addr2,mask;
  int bit;

  if(is_word_type(t)) return 0;      /* SET1/CLR1 operate on one byte */
  if(c!=AND&&c!=OR) return 0;        /* only &= and |= map here */
  if(p->q2.flags!=KONST) return 0;   /* mask must be a bare literal */

  /* q1 and z must resolve to the SAME literal-constant saddr/sfr-
     window address -- i.e. this really is "x op= mask;" (q1 and z
     both parses of the identical lvalue expression), not some other
     IC shape that merely happens to use AND/OR with a KONST q2.
     const_deref_addr() already enforces the KONST|DREFOBJ flag shape
     and the address-window check for each side independently; all
     that's left is confirming they name the same byte. */
  if(!const_deref_addr(&p->q1,0,&addr1)) return 0;
  if(!const_deref_addr(&p->z,0,&addr2)) return 0;
  if(addr1!=addr2) return 0;

  mask=(unsigned long)konst_long(&p->q2,t)&0xffUL;

  if(c==OR){
    /* x |= mask -- native SET1 iff mask has exactly one bit set. */
    if(mask==0||(mask&(mask-1))!=0) return 0;
    for(bit=0;bit<8&&!(mask&(1UL<<bit));bit++);
    emit(f,"\tset1\t");emit_const_addr(f,addr1);emit(f,",%d\n",bit);
  }else{
    /* x &= ~mask -- native CLR1 iff mask has exactly one bit CLEAR,
       i.e. the already ~-complemented, byte-truncated constant the
       frontend handed us (mask) has exactly seven bits set. */
    unsigned long clearbit=(~mask)&0xffUL;
    if(clearbit==0||(clearbit&(clearbit-1))!=0) return 0;
    for(bit=0;bit<8&&!(clearbit&(1UL<<bit));bit++);
    emit(f,"\tclr1\t");emit_const_addr(f,addr1);emit(f,",%d\n",bit);
  }
  return 1;
}

/* ------------------------------------------------------------------
   Native INC/DEC (byte) and INCW/DECW (word) for "x += 1;"/"x -= 1;"
   (equivalently x++/x--/++x/--x, which the frontend lowers to the
   same ADD/SUB IC shape) against a constant saddr/sfr-window address
   (task #48). Same eligibility shape and same reasoning throughout as
   try_native_bit_compound_assign() just above -- deliberately reusing
   that discipline rather than inventing a new one:

    - q1 and z must resolve to the SAME literal-constant saddr/sfr
      address (const_deref_addr()/const_deref_addr_word(), i.e. the
      SFR8()/SFR16()-style macro pattern from task #44 -- never an
      ordinary VAR global, for the identical assemble-time-constant-
      address reason documented there). The register forms of INC/
      DEC/INCW/DECW (OP_R1/OP_RP2) are NOT used here: this backend
      deliberately never keeps a C variable resident in a hardware
      register across IC boundaries (regsa[i]=1 for every register,
      see init_cg()'s comment -- the same finding that grounded task
      #47's deferral), so a source-level variable is never in that
      form at this point regardless of type.
    - q2 must be the literal constant 1 (the only value INC/DEC/INCW/
      DECW can express -- ADD/SUB by any other constant keeps using
      the generic addw/subw path unchanged).

   Volatile-safety: confirmed via the same clock-cycle-table method
   used for SET1/CLR1 (no explicit "read-modify-write" prose exists
   anywhere in the manual, already established by that full-text
   search). The manual's timing table ((11) "Increment/decrement
   instructions") gives INC/DEC saddr 11 clocks and INCW/DECW saddrp
   12 clocks against SFR -- same order of magnitude as SET1/CLR1's 13,
   consistent with the same internal read-then-write bus pattern and
   nothing else. As with SET1/CLR1, the actual guarantee doesn't rest
   on that number: the generic path this replaces (load_byte/
   load_word then add/addw then store_byte/store_word) already
   performs exactly one read and one write for this exact IC shape (z
   is q1), so the substitution cannot change the access count either
   way. */
static int try_native_incdec(FILE *f,struct IC *p,int c,int t)
{
  unsigned long addr1,addr2;
  const char *mn;

  if(c!=ADD&&c!=SUB) return 0;
  if(p->q2.flags!=KONST) return 0;
  if(konst_long(&p->q2,t)!=1) return 0;   /* only the by-1 case maps */

  if(is_word_type(t)){
    if(!const_deref_addr_word(&p->q1,&addr1)) return 0;
    if(!const_deref_addr_word(&p->z,&addr2)) return 0;
    if(addr1!=addr2) return 0;
    mn=(c==ADD)?"incw":"decw";
  }else{
    if(!const_deref_addr(&p->q1,0,&addr1)) return 0;
    if(!const_deref_addr(&p->z,0,&addr2)) return 0;
    if(addr1!=addr2) return 0;
    mn=(c==ADD)?"inc":"dec";
  }
  emit(f,"\t%s\t",mn);emit_const_addr(f,addr1);emit(f,"\n");
  return 1;
}

/* ------------------------------------------------------------------
   General native ADD/SUB (byte or word) of an ARBITRARY compile-time
   constant directly against a constant saddr/sfr-window address --
   "x += N;"/"x -= N;" for any N, not just the by-1 case above (task
   #48, found while auditing for it: shared/Opcodes.h has a genuine
   direct "add/sub saddr,#byte" and "addw/subw saddrp,#word" memory-
   immediate form -- confirmed assemblable with vasm as-is, no new
   backend work needed there, same as every other addressing mode
   already exploited by the saddr/SFR work). Same eligibility shape,
   same reasoning, and the same "never an ordinary VAR global, never a
   register-resident C variable" boundary as try_native_incdec() right
   above -- deliberately factored as a SEPARATE, later check (not
   folded into try_native_incdec()) so the N==1 case keeps preferring
   INC/DEC/INCW/DECW's smaller fixed-operand encoding (2 bytes vs. 3
   for the byte case, 3 vs. 4 for word) -- the caller tries
   try_native_incdec() first and only falls through to this one when
   that declines.

   Volatile-safety: the manual's clock-cycle tables ((1)/(6)-class
   arithmetic sections) give ADD/SUB saddr,#byte 12 clocks against SFR
   and ADDW/SUBW saddrp,#word 13 -- the SAME order of magnitude as
   INC/DEC's 11/12 and SET1/CLR1's 13, again consistent with a single
   internal read-then-write bus cycle and nothing else. As with those,
   the actual guarantee doesn't rest on the clock number: the generic
   load/add/store or load_word/addw/store_word path this replaces
   already performs exactly one read and one write for this exact IC
   shape (z is q1), so the substitution can't change the access
   count either way. */
static int try_native_addsub_konst(FILE *f,struct IC *p,int c,int t)
{
  unsigned long addr1,addr2;
  const char *mn;

  if(c!=ADD&&c!=SUB) return 0;
  if(p->q2.flags!=KONST) return 0;

  if(is_word_type(t)){
    if(!const_deref_addr_word(&p->q1,&addr1)) return 0;
    if(!const_deref_addr_word(&p->z,&addr2)) return 0;
    if(addr1!=addr2) return 0;
    mn=(c==ADD)?"addw":"subw";
  }else{
    if(!const_deref_addr(&p->q1,0,&addr1)) return 0;
    if(!const_deref_addr(&p->z,0,&addr2)) return 0;
    if(addr1!=addr2) return 0;
    mn=(c==ADD)?"add":"sub";
  }
  emit(f,"\t%s\t",mn);emit_const_addr(f,addr1);emit(f,",#");
  emitval(f,&p->q2.val,t&NU);emit(f,"\n");
  return 1;
}

/* Native CMP/CMPW of an arbitrary compile-time constant directly
   against a constant saddr/sfr-window address -- COMPARE's analogue
   of try_native_addsub_konst() just above. COMPARE has no z (it only
   sets flags), so the eligibility check is simpler: just q1 in-window
   and q2 a bare KONST.

   Volatile-safety: CMP saddr,#byte / CMPW saddrp,#word are NOTABLY
   CHEAPER than the corresponding ADD/SUB/ADDW/SUBW forms in the same
   clock tables (8-9 clocks vs. 12-13 against SFR) -- a clean,
   internally-consistent signal that CMP does NOT write its result
   back to memory (a compare has nothing to write back), i.e. exactly
   one read and zero writes. The generic path this replaces
   (load_byte/load_word into a register, then cmp/cmpw against the
   register) already performs exactly one read and no write for this
   IC, so the access count is unchanged either way -- the timing
   delta is corroborating evidence, not the basis for the claim. */
static int try_native_compare_konst(FILE *f,struct IC *p,int t)
{
  unsigned long addr;
  const char *mn;

  if(!(p->q2.flags==KONST)) return 0;
  if(is_word_type(t)){
    if(!const_deref_addr_word(&p->q1,&addr)) return 0;
    mn="cmpw";
  }else{
    if(!const_deref_addr(&p->q1,0,&addr)) return 0;
    mn="cmp";
  }
  emit(f,"\t%s\t",mn);emit_const_addr(f,addr);emit(f,",#");
  emitval(f,&p->q2.val,t&NU);emit(f,"\n");
  return 1;
}

/* ------------------------------------------------------------------
   Native BT/BF branch-on-bit for if(x&BIT)/if(!(x&BIT))/while(x&BIT)/
   while(!(x&BIT)) against a constant saddr/sfr-window address (task
   #50). Scope, per explicit review instruction: control-flow bit
   tests ONLY -- if/while, not "return (x&BIT)!=0" boolean
   materialization (a structurally different, larger IC shape --
   MOVE #0/#1 + CONVERT + SET-RETURN, confirmed separate by direct
   inspection of the same probe matrix cited below -- left for its own
   future task, not touched here).

   vbcc lowers the positive and negated C forms into two GENUINELY
   DIFFERENT IC shapes, not mirror images of each other -- confirmed
   by instrumenting gen_code() with pric2() against a real compiled
   matrix (if/while x [!]& 4, for both a literal SFR8() address and an
   ordinary volatile global -- /tmp/bt_ic_probe/bt_matrix.c /
   ic_dump.txt) rather than assumed, per explicit review instruction
   ("write the peepholes against what vbcc actually emits, rather
   than an assumed IC sequence"). So this is two independent matchers
   below, sharing only the final branch-mnemonic mapping step:

     POSITIVE  if(x&BIT) / while(x&BIT):  3 ICs, byte-typed throughout
       AND  (q1=<addr>,          q2=KONST single-bit,       z=T )
       TEST (q1=T)
       BEQ/BNE(label)

     NEGATED   if(!(x&BIT)) / while(!(x&BIT)):  4 ICs, int-widened
       CONVERT(q1=<addr>,z=T1)  -- char -> int; this is the ONE real
                                    volatile read (the existing char-
                                    >word case's load_byte() call,
                                    already on task #44's fast path
                                    when <addr> qualifies)
       AND    (q1=T1,q2=KONST single-bit as int, z=T2)   -- word-typed
       TEST   (q1=T2)
       BEQ/BNE(label)

   In BOTH shapes the trailing branch condition maps identically --
   BEQ -> BF (branch if bit clear), BNE -> BT (branch if bit set) --
   because the frontend has already fully resolved the C-level
   positive/negated distinction into which specific branch opcode it
   emits (if(x&BIT) skips-on-BEQ, while(x&BIT) continues-on-BNE,
   if(!(x&BIT)) skips-on-BNE, while(!(x&BIT)) continues-on-BEQ -- all
   four confirmed directly in the probe dump). The peephole doesn't
   need to separately reason about "was this negated", only about
   which prefix shape led up to the TEST and which branch opcode
   follows it.

   Eligibility, all required:
    - the tested address must resolve via const_deref_addr() -- the
      SAME literal-constant saddr/sfr-window-address boundary as
      #44/#46/#48, for the identical reason: BT/BF's tested-location
      operand (shared/Opcodes.h: OP_SADDR/OP_SFR) can only take an
      assemble-time-resolved address, never an ordinary linker-placed
      C global or a register-resident variable (this backend keeps
      every C variable memory-resident across IC boundaries -- see
      #47's finding). Confirmed in the very same probe matrix: the
      ordinary-global variant (g_if_pos etc.) produces the IDENTICAL
      IC shape, differing only in q1's flags (VAR vs KONST|DREFOBJ) --
      const_deref_addr() already declines that case correctly with no
      separate check needed here.
    - the mask must have exactly one bit set, AND that bit must be in
      0..7: BT/BF's bit-number field only ever addresses one of the
      eight bits of the single byte at the tested address (shared/
      Opcodes.h's OP_N_BIT field; the manual's "saddr.bit"/"sfr.bit"
      operand notation agrees) -- a mask bit at or above 8 cannot be a
      legal BT/BF operand regardless of int-widening, so this declines
      and falls through to the generic path unchanged. (Bits 0..7 stay
      safe to test after CONVERT's char->int widening regardless of
      the source char's signedness: sign vs. zero extension only ever
      changes bits 8 and up, never bits 0-7, so the widened int's low
      byte -- and therefore any bit 0-7 test on it -- is bit-for-bit
      identical to the original byte either way.)
    - the AND's result temp (z) must be proven dead after the branch.
      This is a purely structural, local proof, not a full liveness
      analysis, but it is sound given how this backend's IC generator
      actually allocates temps (confirmed throughout the entire probe
      dump: every compiler-synthesized temp address is unique per
      occurrence, never reused across statements) -- objs_equal()
      confirms TEST's q1 is the EXACT SAME object as AND's z, and this
      matcher additionally requires that TEST be immediately followed
      by BEQ/BNE with nothing in between. TEST itself never writes
      back to that object (see the TEST case's own comment above: "z
      is unused, exactly as COMPARE leaves it unused"), and the branch
      that follows consumes only the hardware flag results of TEST,
      never the temp's value directly. So the temp's one and only
      definition (this AND) and its one and only consumer (this TEST)
      are being deleted together as an atomic unit; nothing else in
      the IC chain can observe it. The identical argument one level up
      covers CONVERT's T1 in the negated shape (T1's only consumer is
      the very AND being deleted here too).
    - volatile access count: unchanged in both shapes, by the same
      "generic path already performs exactly the accesses this
      replaces" argument used for every prior native-instruction
      substitution this session. Positive shape: the generic path's
      AND already performs exactly one load_byte() of the volatile
      address (TEST/branch afterward touch only the scratch temp,
      never hardware again); BT/BF performs exactly one hardware test.
      Negated shape: the generic path's CONVERT already performs
      exactly one load_byte() of the volatile address (the char->word
      widening case), and everything after (AND/TEST/branch) operates
      purely on the by-then-ordinary scratch int temp; BT/BF again
      performs exactly one hardware test. Read-only, no write-back,
      corroborated independently via the manual's timing tables: BT/BF
      are listed among the plain conditional-branch instructions ((20)
      "Conditional branch instructions"), and their operand encoding
      (shared/Opcodes.h) carries only a source bit-address plus a
      branch-target displacement -- no destination field at all,
      unlike e.g. DBNZ in that very same instruction group, which does
      write back.

   Called from the TOP of gen_code()'s main loop (before the switch),
   because this spans multiple ICs (3 or 4) and must consume them as
   one unit: on success it advances *pp to the LAST IC it consumed
   (the branch), so the caller's own "for(;p;p=p->next)" increment
   lands exactly on the first unconsumed IC (normally the target
   label, which the very next iteration emits completely normally). */
static int try_native_bit_branch(FILE *f,struct IC **pp)
{
  struct IC *p=*pp,*conv_ic,*and_ic,*test_ic,*br_ic;
  unsigned long addr;
  unsigned long mask;
  int bit;

  if(p->code==CONVERT){
    int st=q1typ(p)&NU,dt=ztyp(p)&NU;
    if(is_word_type(st)) return 0;      /* must be a byte->word widen */
    if(!is_word_type(dt)) return 0;
    if(!const_deref_addr(&p->q1,0,&addr)) return 0;
    conv_ic=p;
    and_ic=p->next;
    if(!and_ic||and_ic->code!=AND) return 0;
    if(!is_word_type(and_ic->typf&NU)) return 0;
    if(!objs_equal(&and_ic->q1,&conv_ic->z,and_ic->typf&NU)) return 0;
    if(and_ic->q2.flags!=KONST) return 0;
    mask=(unsigned long)konst_long(&and_ic->q2,and_ic->typf);
  }else if(p->code==AND){
    and_ic=p;
    if(is_word_type(and_ic->typf&NU)) return 0;  /* positive form is byte-typed */
    if(!const_deref_addr(&and_ic->q1,0,&addr)) return 0;
    if(and_ic->q2.flags!=KONST) return 0;
    mask=(unsigned long)konst_long(&and_ic->q2,and_ic->typf)&0xffUL;
  }else return 0;

  test_ic=and_ic->next;
  if(!test_ic||test_ic->code!=TEST) return 0;
  if(!objs_equal(&test_ic->q1,&and_ic->z,and_ic->typf&NU)) return 0;

  br_ic=test_ic->next;
  if(!br_ic||(br_ic->code!=BEQ&&br_ic->code!=BNE)) return 0;

  if(mask==0||(mask&(mask-1))!=0) return 0;  /* not exactly one bit */
  for(bit=0;bit<8&&!(mask&(1UL<<bit));bit++);
  if(bit>=8) return 0;   /* bit outside the byte -- no legal BT/BF operand */

  /* BEQ -> BF (branch if bit clear), BNE -> BT (branch if bit set);
     uniform across both shapes -- see the block comment above.
     Operand order (address, label, bit) confirmed against
     shared/Opcodes.h's field order for M_BT/M_BF (OP_SADDR/OP_SFR,
     OP_ADDR16R, OP_N_BIT) and directly verified by assembling and
     disassembling both "bt sfr[addr],label,bit" and
     "bt <saddr>,label,bit" probes byte-correct. */
  emit(f,"\t%s\t",br_ic->code==BNE?"bt":"bf");
  emit_const_addr(f,addr);
  emit(f,",%s%d,%d\n",labprefix,br_ic->typf,bit);

  *pp=br_ic;    /* caller's loop increment (p=p->next) lands just past this */
  return 1;
}

/* ------------------------------------------------------------------
   Generic boolean-materialization collapse (task #51, follow-on from
   #50). #50's own probe matrix showed sfr_return_bool's "return
   (x&BIT)!=0" surrounded its now-optimized bit test with roughly 20
   instructions of int-sized 0/1 plumbing; auditing a broader matrix
   (==, !=, <, >=, byte and word operands, both "return expr" and
   "var = expr", plus the very common "x != 0"/"x == 0" idioms --
   /tmp/bool_ic_probe/bool_matrix.c, ic_dump.txt) confirms this is NOT
   specific to bit-tests or to "return": vbcc's FRONTEND (not this
   backend) lowers EVERY C boolean-valued expression used in a non-
   branching context into the exact same IC tail, regardless of what
   produced the truth value (COMPARE, TEST, or -- after #50 -- BT/BF):

       ASSIGN(q1=KONST 0,z=T)      int-sized, T a fresh temp -- "move0"
       BRA(Ljoin)
       LABEL(Ltrue)
       ASSIGN(q1=KONST 1,z=T)      same T                     -- "move1"
       LABEL(Ljoin)
       CONVERT(q1=T,z=Z) int->char
       [SETRETURN(q1=Z)]           present for "return expr", absent
                                    for "var = expr" (Z IS the final
                                    destination there: a VAR or an
                                    arbitrary DREFOBJ pointer target)

   (The leading branch that jumps to Ltrue on true/falls through to
   Lfalse otherwise -- COMPARE+Bcc, TEST+Bcc, or #50's own BT/BF -- is
   deliberately left untouched by this peephole: it's already emitted
   by its own case by the time this fires, or already fused away by
   #50. Rewriting it too, to get the fully-minimal "mov a,#0 / bf
   ...,done / inc a" shape the reviewer sketched, would mean reaching
   backward into already-emitted output or inverting a condition
   decided by a completely different, already-executed case -- a real
   architectural change (buffered/lookahead emission), not a narrow
   peephole, and not attempted here. What IS captured below is still
   substantial and completely safe: the entire "materialize 0 or 1
   into a 2-byte int temp, then truncate it back down to the 1-byte
   result" roundtrip, which is pure overhead in every single case in
   the matrix regardless of what generated the condition.)

   Eligibility -- all required, checked before anything is emitted:
    - move0/move1 must target the exact same object (objs_equal()), a
      plain VAR (never DREFOBJ -- confirmed throughout the matrix: the
      int temp is always a fresh frame slot, and objs_equal() plus an
      explicit DREFOBJ exclusion keeps this from ever misfiring on
      some unrelated pointer-target ASSIGN that happens to move a 0
      then later a 1 into the same dereferenced address).
    - move0's value must be exactly 0 and move1's exactly 1 -- this is
      specifically the boolean 0/1 idiom, not some other constant-
      propagation shape that happens to share the branch/label
      skeleton.
    - the BRA between move0 and Ltrue must target the same label the
      LABEL after move1 carries (Ljoin) -- confirms this really is the
      diamond-shaped join this comment describes, not a coincidental
      lookalike.
    - CONVERT must consume the exact same temp (objs_equal() against
      move0/move1's z) and narrow int->char -- the one C-level shape
      this collapses (a byte-sized boolean result; a hypothetical
      int-sized "int flag = (x==y);" would produce a same-size
      CONVERT this backend already treats as a plain copy, a different
      IC shape entirely, not matched here).
    - the eliminated temp T is dead by the same structural argument
      used throughout this file: its ONLY definitions are move0/move1
      (both being deleted here) and its ONLY consumer is the CONVERT
      immediately following the join label (also being deleted), with
      objs_equal() confirming identity at each link and the matcher
      requiring strict IC-to-IC adjacency throughout -- nothing else
      in the chain can observe it.
    - no volatile access at all is involved in this tail (the actual
      volatile read, if any, already happened in whatever produced the
      leading condition, untouched by this peephole) -- so there is
      nothing to preserve here beyond ordinary register correctness.

   Substitution: synthesize the 0/1 result directly in register A
   (instead of a 2-byte int temp) and skip the truncation entirely --
   store_byte()/SETRETURN already accept a value sitting in A exactly
   as readily as one freshly loaded from a temp, since that's always
   been their contract; A just gets filled a cheaper way here.
   "mov a,#0" and "mov a,#1" are both ONE instruction (shared/
   Opcodes.h's MOV A,#byte immediate form) versus move0/move1's
   current 5-instruction ASSIGN-of-a-2-byte-KONST-into-a-stack-slot
   path (load_word()+store_word()), and the CONVERT+[SETRETURN] pair
   -- currently another 8-9 instructions of stack reloads -- collapses
   to either nothing at all (return case, when the ABI's char-return
   register is already A, the common case confirmed in every "return"
   member of the matrix) or one store_byte() call (assignment case,
   handling a VAR or an arbitrary runtime pointer destination exactly
   as the generic path already did). The BRA and the two LABELs are
   deliberately left for their own ordinary per-IC cases (nothing
   about their emission needs to change), so this function only ever
   overrides move0, move1, and CONVERT/[SETRETURN]'s emission.

   Same multi-IC-consumption convention as try_native_bit_branch():
   called from the top of gen_code()'s loop, and on success advances
   *pp to the LAST IC consumed so the caller's own increment lands on
   the first unconsumed one. */
static int try_native_bool_materialize(FILE *f,struct IC **pp)
{
  struct IC *move0=*pp,*bra_ic,*ltrue_ic,*move1,*ljoin_ic,*convert_ic,*setret_ic;
  int tw;   /* the shared int temp's word-typed flag, sanity-checked throughout */

  if(move0->q1.flags!=KONST) return 0;
  if(konst_long(&move0->q1,move0->typf)!=0) return 0;
  if(move0->z.flags&DREFOBJ) return 0;         /* must be a plain temp, never a pointer target */
  tw=is_word_type(move0->typf&NU);
  if(!tw) return 0;                            /* the shared temp is always int-sized in the matrix */

  bra_ic=move0->next;
  if(!bra_ic||bra_ic->code!=BRA) return 0;

  ltrue_ic=bra_ic->next;
  if(!ltrue_ic||ltrue_ic->code!=LABEL) return 0;

  move1=ltrue_ic->next;
  if(!move1||move1->code!=ASSIGN) return 0;
  if(move1->q1.flags!=KONST) return 0;
  if(konst_long(&move1->q1,move1->typf)!=1) return 0;
  if(!is_word_type(move1->typf&NU)) return 0;
  if(!objs_equal(&move1->z,&move0->z,move1->typf&NU)) return 0;

  ljoin_ic=move1->next;
  if(!ljoin_ic||ljoin_ic->code!=LABEL) return 0;
  if(ljoin_ic->typf!=bra_ic->typf) return 0;   /* must be the BRA's own target -- the real join point */

  convert_ic=ljoin_ic->next;
  if(!convert_ic||convert_ic->code!=CONVERT) return 0;
  if(!objs_equal(&convert_ic->q1,&move0->z,move0->typf&NU)) return 0;
  if(!is_word_type(q1typ(convert_ic)&NU)) return 0;   /* source: the int temp */
  if(is_word_type(ztyp(convert_ic)&NU)) return 0;     /* dest: must narrow to a byte */

  setret_ic=convert_ic->next;
  if(setret_ic&&setret_ic->code==SETRETURN){
    if(!objs_equal(&setret_ic->q1,&convert_ic->z,ztyp(convert_ic)&NU)) setret_ic=0;
  }else{
    setret_ic=0;
  }

  /* Everything checked -- now safe to emit. */
  emit(f,"\tmov\ta,#0\n");
  /* bra_ic (BRA Ljoin) and ltrue_ic (LABEL Ltrue) are deliberately left
     for their own ordinary per-IC cases below -- see the block comment
     above for why nothing about their emission needs to change. */
  emit(f,"\tbr\t%s%d\n",labprefix,bra_ic->typf);
  emit(f,"%s%d:\n",labprefix,ltrue_ic->typf);
  emit(f,"\tmov\ta,#1\n");
  emit(f,"%s%d:\n",labprefix,ljoin_ic->typf);

  if(setret_ic){
    /* Matches the existing SETRETURN case's own CHAR branch exactly
       (regtype[]/BSET(regs_modified,...) precedent) -- the only
       difference is the value is already sitting in A, so this is a
       no-op whenever the char-return register IS A (confirmed the
       common case throughout the matrix), and a single register-to-
       register move otherwise. */
    if(regtype[setret_ic->z.reg]->flags==CHAR){
      if(setret_ic->z.reg!=REG_A) emit(f,"\tmov\t%s,a\n",regnames[setret_ic->z.reg]);
      BSET(regs_modified,setret_ic->z.reg);
    }else{
      /* Structurally unreachable (convert_ic already narrowed to
         char, so SETRETURN's target register type must agree) -- but
         if it ever weren't, declining outright is safer than guessing. */
      return 0;
    }
    *pp=setret_ic;
  }else{
    /* Assignment case: convert_ic->z is the real destination (a plain
       VAR or an arbitrary runtime-pointer DREFOBJ, e.g. "*out = ...")
       -- store_byte() already handles every such shape generically,
       exactly as the generic path this replaces already did. */
    store_byte(f,REG_A,&convert_ic->z,ztyp(convert_ic)&NU);
    *pp=convert_ic;
  }
  return 1;
}

void gen_code(FILE *f,struct IC *p,struct Var *v,zmax offset)
{
  struct IC *scan;

  localsize=zm2l(offset);
  argsize=0;
  for(scan=p;scan;scan=scan->next)
    if(scan->code==CALL&&argsize<zm2l(scan->q2.val.vmax))
      argsize=zm2l(scan->q2.val.vmax);
  function_top(f,v,localsize+argsize);

  for(;p;p=p->next){
    int c=p->code,t=p->typf&NU;
    int word=is_word_type(t);

    /* Multi-IC peephole (task #50) -- must be tried before the switch
       below since it spans 3 or 4 ICs (AND/TEST/branch or CONVERT/
       AND/TEST/branch), not just the current one. See the function's
       own block comment for the full design and safety argument. */
    if((c==AND||c==CONVERT)&&try_native_bit_branch(f,&p)) continue;

    /* Multi-IC peephole (task #51) -- generic boolean-materialization
       collapse (0/1 int temp + truncate), spans 6-7 ICs starting at
       an ASSIGN of KONST 0. See the function's own block comment for
       the full design and safety argument. */
    if(c==ASSIGN&&try_native_bool_materialize(f,&p)) continue;

    switch(c){
    case NOP:
    case FREEREG:
    case ALLOCREG:
      break;

    case LABEL:
      emit(f,"%s%d:\n",labprefix,p->typf);
      break;

    case BRA:
      emit(f,"\tbr\t%s%d\n",labprefix,p->typf);
      break;

    case BEQ: case BNE: case BLT: case BGE: case BLE: case BGT:
      /* See last_compare_was_unsigned's own comment above (bug #1 fix):
         p->typf here is this branch's own jump-target label number,
         NOT type info -- the real signedness comes from the COMPARE/
         TEST IC that immediately preceded this one and set the flags
         this branch reads. */
      emit(f,"\t%s\t%s%d\n",
           cond_branch_mnemonic(c,last_compare_was_unsigned?UNSIGNED:0),
           labprefix,p->typf);
      break;

    case COMPARE:
      /* t&UNSIGNED is the one place this IC's REAL operand type is
         available -- stash it now, for whichever BEQ/BNE/BLT/BGE/BLE/
         BGT IC comes right after and reads the flags this sets (see
         last_compare_was_unsigned's comment above cond_branch_mnemonic). */
      last_compare_was_unsigned=(t&UNSIGNED)!=0;
      if(try_native_compare_konst(f,p,t)) break;
      if(word){
        /* NOTE the per-branch load_word(AX,q1,...) placement below --
           NOT hoisted above this if/else the way it originally was.
           See the block comment on ADD/SUB's word case (just below)
           for the real, previously-latent bug this works around: for
           a VAR/DREFOBJ q2, load_word(HL,q2,...) always bounces
           through A internally regardless of destination pair, so
           loading q1 into AX *before* that call gets q1's high byte
           (register A) silently clobbered. q1 must load into AX LAST
           in that branch, mirroring MULT's already-safe "q2 first,
           q1 right before it's needed" ordering. The REG/KONST
           branches never touch A for q2 at all, so they're unaffected
           either way -- q1 loads first there for no particular
           reason beyond matching the original code's shape. */
        if((p->q2.flags&(REG|DREFOBJ))==REG){
          load_word(f,REG_AX,&p->q1,t);
          emit(f,"\tcmpw\tax,%s\n",regnames[p->q2.reg]);
        }else if(p->q2.flags&KONST){
          load_word(f,REG_AX,&p->q1,t);
          emit(f,"\tcmpw\tax,#");emitval(f,&p->q2.val,t&NU);emit(f,"\n");
        }else if((p->q2.flags&VAR)||(p->q2.flags&DREFOBJ)){
          /* cmpw's only memory forms are saddrp/sfrp, not [sp+byte] --
             load q2 into the HL scratch pair FIRST (before q1 ever
             touches AX -- see the comment above). A dereferenced q2
             (DREFOBJ) is resolved into DE by load_word() itself, not
             HL -- see resolve_pointer()'s comment -- so no conflict
             with q1's own possible DREFOBJ use of DE either, since
             q2's dereference is fully consumed before q1 is touched. */
          load_word(f,REG_HL,&p->q2,t);
          load_word(f,REG_AX,&p->q1,t);
          emit(f,"\tcmpw\tax,hl\n");
        }else unsupported_ic(p);
      }else{
        /* need_deref_stash: q2 is a dereferenced pointer (*p). Fully
           resolve it -- pointer setup AND the byte fetch -- into the C
           scratch register as ONE complete, separate step BEFORE q1
           ever touches A. See the matching comment on the ADD/SUB/AND/
           OR/XOR byte case just below for why: building the "cmp\ta,"
           text and then calling emit_var_mem() on a DREFOBJ q2 emits
           resolve_pointer()'s several complete setup lines INSIDE the
           partial "cmp\ta," line, corrupting the output. A plain VAR
           q2 (no DREFOBJ) has no such hazard -- emit_var_mem() for that
           case is one self-contained fprintf with no embedded newline
           -- so it keeps using the direct memory-operand form below,
           unchanged from before. */
        int need_deref_stash=(p->q2.flags&DREFOBJ)!=0;
        if(need_deref_stash) load_byte(f,REG_C,&p->q2,t);
        load_byte(f,REG_A,&p->q1,t);
        emit(f,"\tcmp\ta,");
        if((p->q2.flags&(REG|DREFOBJ))==REG) emit(f,"%s",regnames[p->q2.reg]);
        else if(p->q2.flags&KONST){emit(f,"#");emitval(f,&p->q2.val,t&NU);}
        else if(need_deref_stash) emit(f,"c");
        else if(p->q2.flags&VAR) emit_var_mem(f,&p->q2,0);
        else unsupported_ic(p);
        emit(f,"\n");
      }
      break;

    case TEST:
      /* Per interface.texi: "test q1 against 0 and set condition codes
         ... equivalent to COMPARE q1,#0 but only BEQ/BNE have to be
         set." Our hardware CMP/CMPW against an immediate #0 is a real
         subtraction that sets ALL flags accurately (carry/sign/zero),
         not just zero -- a strict superset of what's required, so this
         is just COMPARE's q1-vs-KONST-0 path with no q2 at all (q1 is
         TEST's only operand; z is unused, exactly as COMPARE leaves it
         unused -- this backend has no multiple_ccs support, branches
         read hardware flags directly). This is what "if(x)"/"while(x)"
         compile to when x isn't already the result of an explicit
         relational comparison -- e.g. t12.c's
         "while (!(UART_STATUS & 0x02))" needed this (IC code 68 wasn't
         yet implemented, confirmed by a live compile failure before
         this case existed).

         Per that same interface.texi line, only BEQ/BNE ever follow a
         TEST (signedness-independent -- Z flag alone), so this doesn't
         strictly need to feed last_compare_was_unsigned at all. Set
         for defensive symmetry with COMPARE anyway, in case a future
         change to this backend ever lets a BLT/BGE/BLE/BGT follow a
         TEST -- costs nothing and removes one way this fix could be
         silently reintroduced later. */
      last_compare_was_unsigned=(t&UNSIGNED)!=0;
      if(word){
        load_word(f,REG_AX,&p->q1,t);
        emit(f,"\tcmpw\tax,#0\n");
      }else{
        load_byte(f,REG_A,&p->q1,t);
        emit(f,"\tcmp\ta,#0\n");
      }
      break;

    case ADD: case SUB: case AND: case OR: case XOR:
      {
        const char *mn;
        if((c==AND||c==OR)&&try_native_bit_compound_assign(f,p,c,t)) break;
        if((c==ADD||c==SUB)&&try_native_incdec(f,p,c,t)) break;
        if((c==ADD||c==SUB)&&try_native_addsub_konst(f,p,c,t)) break;
        switch(c){
        case ADD: mn=word?"addw":"add"; break;
        case SUB: mn=word?"subw":"sub"; break;
        case AND: mn="and"; break; /* no ANDW -- shared/Mnemonic.h has no such mnemonic */
        case OR:  mn="or";  break;
        case XOR: mn="xor"; break;
        default:  mn=0;
        }
        if(word&&(c==AND||c==OR||c==XOR)){
          /* No hardware ANDW/ORW/XORW. Unlike arithmetic (carry/borrow
             propagates between the two bytes), a 16-bit bitwise op has
             NO inter-byte dependency at all, so it decomposes cleanly
             into two independent byte ops -- see gen_word_bitwise(). */
          gen_word_bitwise(f,p,t,mn);
          break;
        }
        if(word){
          /* See COMPARE's word case above for why q1's load_word(AX,..)
             is placed PER-BRANCH rather than hoisted here: for a VAR/
             DREFOBJ q2, load_word(HL,q2,...) always bounces through A
             internally (regardless of destination pair), so loading
             q1 into AX beforehand would get its high byte (register A)
             silently clobbered. Confirmed as a real, previously-latent
             bug via t18.c's idx_int() (return p[i], word case, i a
             runtime variable rather than a register/immediate) --
             traced execution showed q1's high byte replaced by q2's
             high byte right before the addw, corrupting the pointer
             actually added. REG/KONST q2 never touches A, so those
             branches are unaffected either way. */
          if((p->q2.flags&(REG|DREFOBJ))==REG){
            load_word(f,REG_AX,&p->q1,t);
            emit(f,"\t%s\tax,%s\n",mn,regnames[p->q2.reg]);
          }else if(p->q2.flags&KONST){
            load_word(f,REG_AX,&p->q1,t);
            emit(f,"\t%s\tax,#",mn);emitval(f,&p->q2.val,t&NU);emit(f,"\n");
          }else if((p->q2.flags&VAR)||(p->q2.flags&DREFOBJ)){
            /* addw/subw's only memory forms are saddrp/sfrp, not
               [sp+byte] -- load q2 into the HL scratch pair FIRST
               (before q1 ever touches AX -- see above). A dereferenced
               q2 resolves into DE, not HL, so no conflict with q1's
               own possible DREFOBJ use of DE (fully consumed by then
               -- see resolve_pointer()). */
            load_word(f,REG_HL,&p->q2,t);
            load_word(f,REG_AX,&p->q1,t);
            emit(f,"\t%s\tax,hl\n",mn);
          }else unsupported_ic(p);
          store_word(f,REG_AX,&p->z,t);
        }else{
          /* need_deref_stash: see the identical hazard/fix explained in
             COMPARE's byte case just above -- a DREFOBJ q2 must be
             fully resolved into C as its own complete step BEFORE q1
             is loaded into A, never referenced via emit_var_mem() from
             inside an already-started partial instruction line. A
             plain VAR q2 has no such hazard and keeps the direct
             memory-operand form, unchanged. Confirmed as a real,
             previously-unexercised bug via t13.c: "char f(char a,char
             *p){ return a & *p; }" produced garbled output (a
             resolve_pointer mov sequence spliced into the middle of
             the "and\ta," line) before this fix. */
          int need_deref_stash=(p->q2.flags&DREFOBJ)!=0;
          if(need_deref_stash) load_byte(f,REG_C,&p->q2,t);
          load_byte(f,REG_A,&p->q1,t);
          emit(f,"\t%s\ta,",mn);
          if((p->q2.flags&(REG|DREFOBJ))==REG) emit(f,"%s",regnames[p->q2.reg]);
          else if(p->q2.flags&KONST){emit(f,"#");emitval(f,&p->q2.val,t&NU);}
          else if(need_deref_stash) emit(f,"c");
          else if(p->q2.flags&VAR) emit_var_mem(f,&p->q2,0);
          else unsupported_ic(p);
          emit(f,"\n");
          store_byte(f,REG_A,&p->z,t);
        }
      }
      break;

    case ASSIGN:
      if(t==0) ierror(0);
      if(ISSTRUCT(t)||ISUNION(t)||ISARRAY(t)){
        /* struct/union/array assignment (task #26) -- the exact same
           word/is_word_type()-is-meaningless-for-aggregates bug class
           already found and fixed in PUSH (task #25), independently
           discovered here by the same kind of live test: before this
           fix, ASSIGN's struct/union case silently fell through the
           plain byte branch below (word==is_word_type(t), always false
           for STRUCT/UNION) and copied exactly ONE byte regardless of
           the struct's real size -- confirmed live: "b = a;" for a
           4-byte two-int struct produced a single-byte copy, leaving
           the other 3 bytes of b as stale stack garbage (b.x+b.y read
           back 5 instead of the correct 11).

           opsize(p) (q2.val.vmax) is the object's compile-time-constant
           real size for BOTH ASSIGN and PUSH (interface.texi's opsize()
           entry explicitly covers both IC kinds), so this unrolls into
           a straight-line byte-by-byte copy exactly like PUSH's fix.

           UNLIKE PUSH (whose destination is always a plain [sp+N] stack
           slot, never a pointer), ASSIGN's DESTINATION (z) can ALSO be
           DREFOBJ -- e.g. "*dest = *src;" for two struct pointers --
           so up to TWO independent pointers can be live across the
           same copy loop, not just one. resolve_pointer() is hardcoded
           to land its pointer in DE, so when q1 and z are BOTH DREFOBJ,
           z's pointer is resolved into HL instead (a second pointer-
           capable register pair already used for pointer VALUES
           elsewhere in this file, e.g. ADDI2P/SUBIFP's
           load_word(f,REG_HL,&p->q2,POINTER)), leaving q1's pointer
           undisturbed in DE for the whole loop. When only ONE of q1/z
           is DREFOBJ, it goes through the ordinary DE path via
           resolve_pointer()/emit_var_mem(), matching PUSH's precedent.
           Either way, each pointer is resolved ONCE, as its own
           complete statement, before the loop starts -- never inside a
           partially-emitted line -- the same DREFOBJ-interleaving
           discipline documented on PUSH's fix (and on COMPARE's byte
           case before that); emit_var_mem()'s own resolve_pointer()
           call inside the loop then always hits the already-resolved
           short-circuit and prints a clean single-line "[reg+N]".

           ISARRAY(t) is included defensively alongside ISSTRUCT/
           ISUNION: interface.texi's own ASSIGN entry notes "typf ==
           ARRAY" is possible ("should be in automatic initializations")
           -- a normal local array initializer like "int a[3]={1,2,3};"
           was confirmed (by test) to already lower into per-element
           scalar ASSIGN ICs rather than one bulk ARRAY-typed ASSIGN, so
           this branch is not exercised by that common case, but the
           copy loop below is already fully generic to any compile-
           time-constant opsize(p) regardless of which aggregate kind
           it is, so covering the documented ARRAY case costs nothing
           and closes the same silent-truncation gap should the
           frontend ever emit one. */
        long sz=zm2l(p->q2.val.vmax),i;
        int q1_is_ptr=(p->q1.flags&DREFOBJ)!=0;
        int z_is_ptr=(p->z.flags&DREFOBJ)!=0;
        int z_in_hl=0;
        if(q1_is_ptr) resolve_pointer(f,&p->q1,POINTER);
        if(z_is_ptr){
          if(q1_is_ptr){
            /* DE is already committed to q1's pointer for the rest of
               this loop -- resolve z's pointer into HL instead, via
               the same raw load_word() step resolve_pointer() itself
               performs internally, just targeting the other register
               pair. Deliberately NOT done by mutating p->z into
               REG|DREFOBJ/REG_HL and then continuing to call
               emit_var_mem() on it below, the way the DE path does --
               that was tried first and is a real bug: emit_var_mem()'s
               DREFOBJ branch unconditionally calls resolve_pointer(),
               whose short-circuit only recognizes o->reg==REG_DE, NOT
               REG_HL, so it would NOT treat an already-HL-resident
               pointer as already resolved. It would instead re-resolve
               it by emitting "movw de,hl" -- INSIDE the not-yet-
               terminated "\tmov\t" destination-operand line (the exact
               DREFOBJ-interleaving hazard this whole discipline exists
               to avoid) -- and, far worse, that move overwrites DE,
               destroying q1's own already-resolved pointer for every
               remaining loop iteration. Confirmed live: produced
               garbled output ("mov\t\tmovw\tde,hl\n[de+0],a") that
               vasm rejected. So the HL case is instead tracked with
               its own local flag and its "[hl+N]" text is emitted
               directly below, never through emit_var_mem()/
               resolve_pointer() at all. */
            struct obj tmp=p->z;
            tmp.flags&=~DREFOBJ;
            load_word(f,REG_HL,&tmp,POINTER);
            z_in_hl=1;
          }else{
            resolve_pointer(f,&p->z,POINTER);
          }
        }
        for(i=0;i<sz;i++){
          emit(f,"\tmov\ta,");
          emit_var_mem(f,&p->q1,i);
          emit(f,"\n");
          emit(f,"\tmov\t");
          if(z_in_hl) emit(f,"[hl+%ld]",i);
          else        emit_var_mem(f,&p->z,i);
          emit(f,",a\n");
        }
      }else if(word){ load_word(f,REG_AX,&p->q1,t); store_word(f,REG_AX,&p->z,t); }
      else    { load_byte(f,REG_A,&p->q1,t);  store_byte(f,REG_A,&p->z,t); }
      break;

    case MINUS:
      /* No hardware NEG; 0-x via SUB/SUBW with a #0-preloaded accumulator. */
      if(word){
        emit(f,"\tmovw\tax,#0\n");
        load_word(f,REG_HL,&p->q1,t);
        emit(f,"\tsubw\tax,hl\n");
        store_word(f,REG_AX,&p->z,t);
      }else{
        /* q1 into C BEFORE setting A=0 -- see load_byte()'s comment on
           the bounce-through-A hazard. The original order (A=0 first,
           then load_byte(...,REG_C,...)) silently overwrote the 0 with
           q1's own value on its way through A, making this always
           compute q1-q1=0 regardless of q1 -- a real bug, caught by
           t8.c since no earlier test exercised unary minus at all. */
        load_byte(f,REG_C,&p->q1,t);
        emit(f,"\tmov\ta,#0\n");
        emit(f,"\tsub\ta,c\n");
        store_byte(f,REG_A,&p->z,t);
      }
      break;

    case KOMPLEMENT:
      /* Bitwise complement (~x), q1->z. No hardware COM/NOT byte-wide
         instruction exists (only M_NOT1, a single-BIT complement --
         confirmed by a full Opcodes.h dump: every M_NOT1 row targets
         one bit via OP_N_BIT_2, none targets a whole byte/word). ~x is
         just x^ALL_ONES, and XOR is already available, so no separate
         helper/branch tree is needed here at all -- unlike ADD/SUB/AND/
         OR/XOR's byte case, q1 is this IC's ONLY operand (there is no
         q2 to build op-line text around), so none of that case's
         DREFOBJ-interleaving hazard applies: load_word()/load_byte()
         each fully resolve q1 (pointer dereference included) as one
         complete, self-contained step before anything else runs. */
      if(word){
        load_word(f,REG_AX,&p->q1,t);
        emit(f,"\txor\ta,#0xff\n");
        emit(f,"\tmov\th,a\n");
        emit(f,"\tmov\ta,x\n");
        emit(f,"\txor\ta,#0xff\n");
        emit(f,"\tmov\tl,a\n");
        emit(f,"\tmovw\tax,hl\n");
        store_word(f,REG_AX,&p->z,t);
      }else{
        load_byte(f,REG_A,&p->q1,t);
        emit(f,"\txor\ta,#0xff\n");
        store_byte(f,REG_A,&p->z,t);
      }
      break;

    case MULT:
      /* No signed multiply exists in hardware at all (MULW is in the
         manual's instruction list but not in shared/Mnemonic.h/
         Opcodes.h -- confirmed absent by a full-file dump -- so it was
         apparently never wired into our shared assembler/disassembler
         source of truth and isn't available to emit here regardless).
         Not needed anyway: the low N bits of an N-bit*N-bit two's-
         complement product are bit-identical whether the operands are
         taken as signed or unsigned, and C's a*b truncates to the
         result's own width -- so the unsigned MULU/MULUW give exactly
         the right bit pattern for signed multiply too. */
      if(word){
        /* MULUW rp1: AX(high 16 bits), rp1(low 16 bits) <- AX * rp1 --
           the full 32-bit product split across two pairs (confirmed
           against the real instruction manual, not the scanned/
           non-text datasheet). C's int*int truncates to the low 16
           bits, which is the *rp1* half after the instruction, not AX.
           Load q2 into HL (never AX, which q1 needs; never DE, which
           is resolve_pointer()'s reserved scratch) first, then q1 into
           AX LAST right before it's needed, multiply, then relocate
           HL's (the truncated result's) value into AX -- store_word()
           requires an r1-eligible pair (AX/BC), which HL is not; see
           its own comment. */
        load_word(f,REG_HL,&p->q2,t);
        load_word(f,REG_AX,&p->q1,t);
        emit(f,"\tmuluw\thl\n");
        emit(f,"\tmovw\tax,hl\n");
        store_word(f,REG_AX,&p->z,t);
      }else{
        /* MULU r1: A * r1 -> AX (full 16-bit product). C's char*char
           truncates to the low byte, which lands in X (AX's low half
           -- see reg_pair()). Load q2 into C (a non-A destination)
           FIRST, then q1 into A LAST -- see load_byte()'s comment on
           the bounce-through-A hazard (the same hazard MINUS's byte
           case was bitten by). */
        load_byte(f,REG_C,&p->q2,t);
        load_byte(f,REG_A,&p->q1,t);
        emit(f,"\tmulu\tc\n");
        store_byte(f,REG_X,&p->z,t);
      }
      break;

    case DIV: case MOD:
      {
        int ismod=(c==MOD);
        int uns=(t&UNSIGNED)!=0;
        if(word){
          if(uns) gen_unsigned_word_divmod(f,p,t,ismod);
          else    gen_signed_word_divmod(f,p,t,ismod);
        }else{
          if(uns) gen_unsigned_byte_divmod(f,p,t,ismod);
          else    gen_signed_byte_divmod(f,p,t,ismod);
        }
      }
      break;

    case LSHIFT: case RSHIFT:
      {
        int uns=(t&UNSIGNED)!=0;
        int rshift=(c==RSHIFT);
        if(p->q2.flags&KONST){
          /* Compile-time-constant count: the direct, no-loop path --
             see the block comment above gen_byte_shift()/gen_word_shift(). */
          long cnt=konst_long(&p->q2,q2typ(p));
          if(cnt<0) cnt=0; /* negative shift count is UB in C; clamp rather than emit nonsense */
          if(word) gen_word_shift(f,p,t,rshift,uns,cnt);
          else     gen_byte_shift(f,p,t,rshift,uns,cnt);
        }else{
          /* Runtime-variable count -- see the block comment above
             gen_byte_shift_var()/gen_word_shift_var() (task #24). */
          int q2word=is_word_type(q2typ(p)&NU);
          if(word) gen_word_shift_var(f,p,t,rshift,uns,q2word);
          else     gen_byte_shift_var(f,p,t,rshift,uns,q2word);
        }
      }
      break;

    case ADDI2P: case SUBIFP:
      /* Add/subtract an integer to/from a pointer (array indexing,
         pointer walking: p+i, p-i, p++, etc). Per interface.texi:
         q1,z are always pointers (typf2) and q2 is an integer of type
         typf -- q2 is ALREADY the byte offset (any element-size
         scaling for p+i on a >1-byte element type happens upstream,
         before this IC is generated -- confirmed by machine.h's
         MINADDI2P/MAXADDI2P both being INT, which only forces q2's
         WIDTH, not any scaling factor here). Both operands are
         therefore always word-sized already, making this nothing more
         than a plain word add/sub -- exactly ADD/SUB's own word case,
         just with q1/z's type hardcoded to POINTER (matching the
         existing convention in ADDRESS/CALL below, which already pass
         the literal POINTER constant rather than deriving it from
         p->typf/p->typf2) instead of reading it from t (which for
         THIS ic is q2's int type, per the doc -- gen_code()'s
         top-of-loop t/word are computed from p->typf uniformly for
         every IC, and happen to already be exactly right for q2 here).

         q1's load_word(AX,...) is placed PER-BRANCH below, not hoisted
         above the q2-flags dispatch -- see the block comment on ADD/
         SUB's own word case for the real bug this avoids: for a VAR/
         DREFOBJ q2, load_word(HL,q2,...) always bounces through A
         internally, so loading q1 into AX first would get its high
         byte silently clobbered before the addw/subw runs. Confirmed
         as a real bug via t18.c's idx_int(int*,int) (array indexing
         with a runtime-variable index living in a stack temp, i.e. a
         VAR q2) -- traced execution showed exactly this corruption
         before this ordering fix. */
      {
        const char *mn=(c==ADDI2P)?"addw":"subw";
        if((p->q2.flags&(REG|DREFOBJ))==REG){
          load_word(f,REG_AX,&p->q1,POINTER);
          emit(f,"\t%s\tax,%s\n",mn,regnames[p->q2.reg]);
        }else if(p->q2.flags&KONST){
          load_word(f,REG_AX,&p->q1,POINTER);
          emit(f,"\t%s\tax,#",mn);emitval(f,&p->q2.val,t);emit(f,"\n");
        }else if((p->q2.flags&VAR)||(p->q2.flags&DREFOBJ)){
          load_word(f,REG_HL,&p->q2,t);
          load_word(f,REG_AX,&p->q1,POINTER);
          emit(f,"\t%s\tax,hl\n",mn);
        }else unsupported_ic(p);
        store_word(f,REG_AX,&p->z,POINTER);
      }
      break;

    case SUBPFP:
      /* Pointer difference (q1 - q2, in BYTES -- per interface.texi,
         NOT divided by the pointed-to type's size; if the C-level
         expression needs an element count rather than a byte count,
         the front end chains a separate DIV by the element size after
         this IC, which is not this backend's concern). q1,q2 are
         pointers (word); z is a plain int (typf, word on this target)
         -- so z is stored with t (already p->typf, z's own type here),
         not POINTER. Per-branch q1 load ordering: see ADDI2P/SUBIFP's
         comment just above -- identical hazard, identical fix. */
      if((p->q2.flags&(REG|DREFOBJ))==REG){
        load_word(f,REG_AX,&p->q1,POINTER);
        emit(f,"\tsubw\tax,%s\n",regnames[p->q2.reg]);
      }else if(p->q2.flags&KONST){
        load_word(f,REG_AX,&p->q1,POINTER);
        emit(f,"\tsubw\tax,#");emitval(f,&p->q2.val,POINTER);emit(f,"\n");
      }else if((p->q2.flags&VAR)||(p->q2.flags&DREFOBJ)){
        load_word(f,REG_HL,&p->q2,POINTER);
        load_word(f,REG_AX,&p->q1,POINTER);
        emit(f,"\tsubw\tax,hl\n");
      }else unsupported_ic(p);
      store_word(f,REG_AX,&p->z,t);
      break;

    case ADDRESS:
      /* Address-of a variable (no dereference here -- just computing
         the pointer value). Locals: SP+real_offset(). Statics/externs:
         the label itself is already an address-valued expression. */
      if(isauto(p->q1.v->storage_class)){
        long off=real_offset(&p->q1);
        emit(f,"\tmovw\tax,sp\n");
        if(off) emit(f,"\taddw\tax,#%ld\n",off);
        store_word(f,REG_AX,&p->z,POINTER);
      }else{
        emit(f,"\tmovw\tax,#");
        emit_obj_bare(f,&p->q1,POINTER);
        emit(f,"\n");
        store_word(f,REG_AX,&p->z,POINTER);
      }
      break;

    case CONVERT:
      {
        int st=q1typ(p)&NQ,dt=ztyp(p)&NQ;
        if(ISFLOAT(st)||ISFLOAT(dt)) unsupported_ic(p);
        if(st==dt||(is_word_type(st)&&is_word_type(dt))){
          if(is_word_type(dt)){ load_word(f,REG_AX,&p->q1,st); store_word(f,REG_AX,&p->z,dt); }
          else                { load_byte(f,REG_A,&p->q1,st);  store_byte(f,REG_A,&p->z,dt); }
        }else if(st==CHAR&&is_word_type(dt)){
          /* Widen a char into a register pair: X gets the value (X is
             AX's low half -- see reg_pair()), A gets the extension.
             The immediate-compare form is fixed to A (OP_A,OP_BYTE --
             "cmp x,#0" is not real 78K3 syntax, only "cmp a,#0" is),
             so the sign test has to happen on A, with "mov x,a" done
             right after (cmp doesn't touch a) before a is overwritten
             with the extension byte. */
          load_byte(f,REG_A,&p->q1,st);
          if(q1typ(p)&UNSIGNED){ /* st was already masked with &NQ above, which clears UNSIGNED -- test the unmasked type */
            emit(f,"\tmov\tx,a\n");
            emit(f,"\tmov\ta,#0\n");
          }else{
            int lab=++label;
            emit(f,"\tcmp\ta,#0\n");
            emit(f,"\tmov\tx,a\n");
            emit(f,"\tbn\t%s%d\n",labprefix,lab);
            emit(f,"\tmov\ta,#0\n");
            emit(f,"\tbr\t%s%d\n",labprefix,lab+1);
            emit(f,"%s%d:\n",labprefix,lab);
            emit(f,"\tmov\ta,#0xff\n");
            emit(f,"%s%d:\n",labprefix,lab+1);
            label++;
          }
          store_word(f,REG_AX,&p->z,dt);
        }else if(is_word_type(st)&&dt==CHAR){
          load_word(f,REG_AX,&p->q1,st);
          store_byte(f,REG_X,&p->z,dt);
        }else{
          unsupported_ic(p);
        }
      }
      break;

    case GETRETURN:
      if(p->q1.reg){
        /* regtype[] only ever holds exactly &chtyp or &ityp for our
           registers (see init_cg()) -- a bitmask test here would be
           wrong since CHAR/SHORT/INT/POINTER are plain sequential
           ints (1/2/3/10), not independent bits. */
        if(regtype[p->q1.reg]->flags==CHAR) store_byte(f,p->q1.reg,&p->z,t);
        else store_word(f,p->q1.reg,&p->z,t);
      }
      break;

    case SETRETURN:
      if(regtype[p->z.reg]->flags==CHAR) load_byte(f,p->z.reg,&p->q1,t);
      else load_word(f,p->z.reg,&p->q1,t);
      BSET(regs_modified,p->z.reg);
      break;

    case MOVETOREG:
      if(regtype[p->z.reg]->flags==CHAR) load_byte(f,p->z.reg,&p->q1,t);
      else load_word(f,p->z.reg,&p->q1,t);
      break;

    case MOVEFROMREG:
      if(regtype[p->z.reg]->flags==CHAR) store_byte(f,p->z.reg,&p->q1,t);
      else store_word(f,p->z.reg,&p->q1,t);
      break;

    case PUSH:
      /* No real hardware push available to us (see the argsize/pushed
         comment above real_offset()) -- just write into the next slot
         of this function's pre-sized argument-build area at [sp+
         pushed]. q1 can legitimately be empty ("used for ABIs which
         require stack-slots to be omitted", per interface.texi) --
         still advance pushed by pushsize so later slots land right,
         but write nothing in that case. */
      if(p->q1.flags){
        if(ISSTRUCT(t)||ISUNION(t)){
          /* struct/union-by-value argument (task #25) -- interface.texi:
             "q1 does not have to be an elementary type" for PUSH. Before
             this fix, `word` (is_word_type(t), meaningless for a struct
             type -- always false) silently routed this through the
             ordinary byte-scalar branch below, copying exactly ONE byte
             of the struct into the argument slot regardless of its real
             size -- a real, silent miscompilation (confirmed live: a
             4-byte two-int struct argument came out as a single copied
             byte, with the other 3 bytes of the callee's copy left as
             whatever garbage already sat in the caller's pre-sized
             argument-build area). ISSTRUCT()/ISUNION() (supp.h) catch
             this BEFORE the word/byte dispatch, regardless of what
             is_word_type() would have said.

             opsize(p) (q2.val.vmax) is the object's real size, always a
             compile-time constant (a struct/union's size never varies
             at runtime) -- so the copy unrolls into a straight-line
             byte-by-byte sequence, no runtime loop needed (unlike the
             runtime SHIFT count above, where the count truly isn't
             known until runtime). q1 may be VAR (the struct's own
             [sp+N] or static storage) or DREFOBJ (through a struct
             pointer, e.g. passing *p by value) -- emit_var_mem() (the
             same helper load_byte()/store_byte() use internally for a
             single byte) already handles both transparently, including
             DREFOBJ's one-time resolve_pointer() call into DE that
             every subsequent byteoff here automatically reuses without
             re-resolving (see resolve_pointer()'s own short-circuit
             comment: once o->reg==REG_DE, later calls on the same o are
             a no-op).

             pushsize(p) (z.val.vmax, used below exactly as before) may
             be larger than opsize(p) due to alignment padding, per
             interface.texi -- only the real opsize(p) bytes are copied
             here; any pad bytes up to pushsize are left unwritten
             (never read back through a real struct member, so their
             value is immaterial), and `pushed` still advances by the
             full pushsize so later argument slots land at the right
             offset, matching the existing scalar branches' comment.

             BUG FOUND AND FIXED: the first cut of this loop called
             emit_var_mem() directly inside an already-partially-written
             "\tmov\ta," line (see below) without resolving a DREFOBJ q1
             first. For the VAR case that's harmless (emit_var_mem's VAR
             branch is a pure, single-shot text formatter with no side
             emission), but for the DREFOBJ case (struct passed via a
             pointer dereference, e.g. sum_by_value(*pp)) emit_var_mem's
             *first* call internally calls resolve_pointer(), which
             itself emits several COMPLETE lines of setup text ("mov
             a,[sp+8]\n\tmov e,a\n\tmov a,[sp+9]\n\tmov d,a\n") -- and
             that multi-line output got spliced into the middle of the
             not-yet-terminated "\tmov\ta," prefix, producing malformed
             assembly (confirmed live: vasm errored with "illegal
             operand types >\tmov\ta,\tmov\ta,[sp+8]" and "identifier
             expected >[de+0]"). This is the same DREFOBJ-interleaving
             hazard documented elsewhere in this file for COMPARE's byte
             case: any helper that can internally call resolve_pointer()
             must be given the chance to do so as its OWN complete,
             separate statement, never spliced into a partially-emitted
             line. Fixed by resolving q1 once, up front, before the loop
             starts -- after which resolve_pointer()'s own short-circuit
             (returns immediately once o->flags==REG|DREFOBJ and
             o->reg==REG_DE) makes every emit_var_mem() call inside the
             loop, including the very first, a clean single-line
             "[de+N]" print with no interleaved setup text. */
          long sz=zm2l(p->q2.val.vmax),i;
          if(p->q1.flags&DREFOBJ) resolve_pointer(f,&p->q1,POINTER);
          for(i=0;i<sz;i++){
            emit(f,"\tmov\ta,");
            emit_var_mem(f,&p->q1,i);
            emit(f,"\n");
            emit(f,"\tmov\t");
            emit_sp_off(f,pushed+i); /* bounds-checked -- see its own comment */
            emit(f,",a\n");
          }
        }else if(word){
          load_word(f,REG_AX,&p->q1,t); store_word_at_sp(f,REG_AX,pushed);
        }else{
          load_byte(f,REG_A,&p->q1,t);  store_byte_at_sp(f,REG_A,pushed);
        }
      }
      pushed+=zm2l(p->z.val.vmax); /* pushsize(p) */
      break;

    case CALL:
      /* __asm("...") support: vbcc's generic __asm(x) macro (defined in
         main.c) expands to a static function whose body is literal
         assembly text, stashed in that function's Var->fi->inline_asm
         by declaration.c's special "=string" function-definition
         parsing. The macro still compiles down to an ordinary CALL IC
         targeting that function -- it only actually emits the literal
         text instead of a real "call" instruction if the BACKEND'S own
         CALL case checks for it (every other vbcc backend already has
         this check -- confirmed via grep on minx/machine.c, mirrored
         below). Without this, __asm(...) silently compiled to a real
         "call" to a function that's never defined anywhere, rather
         than erroring OR working -- it would only surface as a link
         failure, not a clear diagnostic. Checked first since it's a
         VAR-only q1 (no DREFOBJ), disjoint from the two cases below. */
      if((p->q1.flags&(VAR|DREFOBJ))==VAR&&p->q1.v->fi&&p->q1.v->fi->inline_asm){
        emit_inline_asm(f,p->q1.v->fi->inline_asm);
      }else if(p->q1.flags&DREFOBJ){
        /* Indirect call through a function pointer. Empirically
           confirmed (a live compile of "int call_through_ptr(fnptr
           fp,int x){ return fp(x); }" with a temporary debug dump)
           that q1.flags==VAR|DREFOBJ here -- i.e. the SAME *shape* as
           an ordinary dereferenced data pointer (fp is stored
           somewhere, VAR-class, and DREFOBJ marks "the object fp
           refers to", per interface.texi) -- but NOT the same
           *handling*: this must NOT be routed through load_word()'s
           general DREFOBJ branch, despite the shape match, because
           that branch does TWO memory reads (resolve_pointer() to
           fetch fp's own stored value into DE, THEN an additional
           read of the two bytes AT that address into the destination
           pair) -- correct for a data pointer, where "the object
           pointed to" is a value sitting in memory at that address,
           but wrong here: a function's "value" (what fp holds) IS
           already its entry address, so getting from "where fp is
           stored" to "the address to call" takes exactly ONE read
           (fp's own stored value, which is what resolve_pointer()
           alone computes), not two. Routing through load_word() was
           tried first and produced a call to whatever raw byte(s)
           happen to sit at the function's own entry point (its first
           instruction, misread as a pointer value) instead of the
           function's address -- caught immediately by simulator
           execution (call target resolved to an address with no
           mapped label, since the simulator, correctly, doesn't
           model code as readable data). Confirmed correct by
           re-simulating call_through_ptr(add1,10) end-to-end: DE ends
           up holding _add1's own synthetic address and "call de"
           lands exactly on _add1's entry.
           resolve_pointer() always lands the pointer in DE (see its
           own comment) and rewrites p->q1 in place to REG|DREFOBJ/
           REG_DE -- calling "call de" directly on that, rather than
           relocating through load_word()/AX, avoids the spurious
           extra dereference entirely. DE is free to use here (not a
           frame/argument register at this point): any real argument
           values were already fully pushed to their [sp+byte] slots
           by earlier PUSH ICs before this CALL IC runs. The
           hardware's REGISTER-DIRECT call form -- "call rp1",
           confirmed in shared/Opcodes.h as a distinct row from the
           register-INDIRECT "call [rp1]" form (OP_RP1_2 vs
           OP_RP1P_2), and shared/RegisterNames.h's REGNAMES_RP1 table
           confirms "de" is a valid rp1 operand -- calls the address
           the register holds directly; "call [de]" would wrongly
           dereference AGAIN, same mistake as the load_word() route. */
        resolve_pointer(f,&p->q1,t);
        emit(f,"\tcall\tde\n");
      }else if((p->q1.flags&(VAR|REG|KONST))==VAR){
        int callt_slot=callt_resolve(p->q1.v);
        if(callt_slot>=0){
          /* Callee is __callt_slotN -- see the big comment above
             g_attr_name[]. Works identically whether this call targets a
             function we're compiling (a table entry gets emitted for it
             in cleanup_cg(), via function_top()'s callt_slot_claim()) or
             one only declared here (a resident/mask-ROM routine, or a
             hand-written shim to one, whose slot Kenwood's own code -- or
             our own hand-assembled table -- already populates): either
             way, the call site only needs the slot NUMBER, never the
             callee's actual address, which is the entire point of CALLT. */
          emit(f,"\tcallt\t[0x%02x]\n",callt_slot);
        }else{
          emit(f,"\tcall\t");
          emit_obj_bare(f,&p->q1,t);
          emit(f,"\n");
        }
      }else unsupported_ic(p);
      pushed=0; /* this call's argument-build slots are free for the next one */
      break;

    default:
      unsupported_ic(p);
    }
  }

  function_bottom(f,v,localsize+argsize);
}

/* ------------------------------------------------------------------ */
/* Static-storage emission.                                            */
/* ------------------------------------------------------------------ */
void gen_ds(FILE *f,zmax size,struct Typ *t)
{
  emit(f,"\t.space\t%ld\n",zm2l(size));
  newobj=0;
}

void gen_align(FILE *f,zmax al)
{
  if(zm2l(al)>1) emit(f,"\t.align\t1\n");
}

void gen_var_head(FILE *f,struct Var *v)
{
  int constflag=0;
  if(v->clist) constflag=is_const(v->vtyp);
  if(v->storage_class==STATIC){
    if(ISFUNC(v->vtyp->flags)) return;
    if(v->clist&&section!=DATA){emit(f,dataname);section=DATA;}
    if(!v->clist&&section!=BSS){emit(f,bssname);section=BSS;}
    gen_align(f,falign(v->vtyp));
    emit(f,"%s%ld:\n",labprefix,zm2l(v->offset));
    newobj=1;
  }
  if(v->storage_class==EXTERN){
    emit(f,"\t.global\t%s%s\n",idprefix,v->identifier);
    if(v->flags&(DEFINED|TENTATIVE)){
      if(v->clist&&section!=DATA){emit(f,dataname);section=DATA;}
      if(!v->clist&&section!=BSS){emit(f,bssname);section=BSS;}
      gen_align(f,falign(v->vtyp));
      emit(f,"%s%s:\n",idprefix,v->identifier);
      newobj=1;
    }
  }
}

void gen_dc(FILE *f,int t,struct const_list *p)
{
  int sz=zm2l(sizetab[t&NQ]);
  emit(f,sz==1?"\t.byte\t":"\t.word\t");
  if(!p->tree){
    emitval(f,&p->val,t&NU);
  }else{
    /* Initializer data: an address constant here is the label itself,
       with no '#' (that is an instruction-operand prefix, not data). */
    emit_obj_bare(f,&p->tree->o,t&NU);
  }
  emit(f,"\n");
  newobj=0;
}
