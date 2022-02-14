#pragma once
#include "Util.h"

#define CARRY_FLAG (1 << 0)
#define SUBTRACT_FLAG (1 << 1)
#define PARITY_FLAG (1 << 2)
#define HALF_CARRY_FLAG (1 << 4)
#define ZERO_FLAG (1 << 6)
#define SIGN_FLAG (1 << 7)


union Register{
	struct {
		u8 lo;
		u8 hi;
	};
	u16 value;
};

struct ShadowedRegisters {
	union Register af;
	union Register bc;
	union Register de;
	union Register hl;
};

struct Z80 {
	struct ShadowedRegisters shadowedregs;
	union Register af;
	union Register bc;
	union Register de;
	union Register hl;
	union Register ix;
	union Register iy;
	union Register ir;
	u16 sp;
	u16 pc;

	u16 cycles;
};

struct Bus* memBus;

void z80Init(struct Z80* z80);
void z80ConnectBus(struct Bus* bus);
void z80SetFlag(struct Z80* z80, u8 flags);
void z80SetFlagCond(struct Z80* z80, u8 cond, u8 flags);

u8 z80ReadU8(u16 address);
u8 z80FetchU8(struct Z80 *z80);
u16 z80ReadU16(u16 address);
u16 z80FetchU16(struct Z80* z80);
u16 z80Clock(struct Z80* z80);

void executeInstruction(struct Z80* z80, u8 opcode);
void executeMainInstruction(struct Z80* z80, u8 opcode);
void executeBitInstruction(struct Z80* z80, u8 opcode);

void executeIxInstruction(struct Z80* z80, u8 opcode);
void executeIxBitInstruction(struct Z80* z80, u8 opcode);

void executeExtendedInstruction(struct Z80* z80, u8 opcode);

void executeIyInstruction(struct Z80* z80, u8 opcode);
void executeIyBitInstruction(struct Z80* z80, u8 opcode);


void ld_reg16(struct Z80* z80);
