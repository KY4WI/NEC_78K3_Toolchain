#ifndef NEC78K3_OPCODE_H
#define NEC78K3_OPCODE_H

#include "nec78k3_types.h"
#include "nec78k3_mnemonic.h"
#include "nec78k3_operand.h"

typedef struct Opcode
{
	Mnemonic mnemonic;
	byte bytes;

	byte op;
	byte opMask;
	byte subOp;
	byte subOpMask;

	Operand operand1;
	Operand operand2;

	int specialOnebyteInstr;

	Operand operand3;
} Opcode;

#endif /* NEC78K3_OPCODE_H */
