#pragma once

#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "Types.h"
#include "Mnemonic.h"
#include "Operand.h"

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

	bool specialOnebyteInstr;

	Operand operand3;
} Opcode;

static void opcode_print_mnemonic(const Opcode *op)
{
	char name[32];
	int nameLen, i;

	strcpy(name, mnemonic_names[(int)op->mnemonic]);
	nameLen = (int)strlen(name);
	for (i = 0; i < nameLen; i++)
	{
		name[i] = (char)tolower((unsigned char)name[i]);
	}
	printf("%-8s", name);
}
