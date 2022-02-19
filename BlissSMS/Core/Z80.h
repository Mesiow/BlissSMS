#pragma once
#include "Util.h"
#include <assert.h>

#define FLAG_C (1 << 0)
#define FLAG_N (1 << 1)
#define FLAG_PV (1 << 2)
#define FLAG_H (1 << 4)
#define FLAG_Z (1 << 6)
#define FLAG_S (1 << 7)

#define NMI_VECTOR 0x66
#define INT_VECTOR 0x38


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


//Processing modes for maskable interrupts
enum IntMode {
	Zero, One, Two
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

	enum IntMode interrupt_mode;
	u16 cycles;
	u8 iff1;
	u8 iff2;
	u8 process_interrupt_delay; //flag used for instruction delay after ei is executed
};

struct Bus* memBus;
struct Io* ioBus;

void z80Init(struct Z80* z80);
void z80ConnectBus(struct Bus* bus);
void z80ConnectIo(struct Io* io);
void z80AffectFlag(struct Z80* z80, u8 cond, u8 flags);
void z80SetFlag(struct Z80* z80, u8 flags);
void z80ClearFlag(struct Z80* z80, u8 flags);
u8 getFlag(struct Z80* z80, u8 flag);

void z80HandleInterrupts(struct Z80* z80);
void z80HandleNonMaskableInterrupt(struct Z80* z80);

u8 z80OverflowFromAdd(u8 op1, u8 op2);
u8 z80OverflowFromSub(u8 op1, u8 op2);
u8 z80IsEvenParity(u8 value);
u8 z80IsSigned(u8 value);

void z80WriteU8(u8 value, u16 address);

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

//Loads
void loadReg16(struct Z80* z80, union Register *reg);
void loadReg8(struct Z80* z80, u8 *reg);
void loadHL8(struct Z80* z80);
void loadAReg(struct Z80* z80, u8 reg);
void loadRegHL(struct Z80* z80, u8* reg);
void loadMemReg16(struct Z80* z80, union Register *reg);
void loadMemReg8(struct Z80* z80, u8 reg);
void loadHlReg(struct Z80* z80, u8 reg);


//Arithmetic
void dec16(struct Z80* z80, union Register* reg);
void dec8(struct Z80* z80, u8* reg);

//Branches/Jumps/Returns
void jrImm(struct Z80* z80);
void jrImmCond(struct Z80* z80, u8 cond);
void djnz(struct Z80* z80);
void rst(struct Z80* z80, u8 vector);
void call(struct Z80* z80);
void callCond(struct Z80* z80, u8 cond);
void ret(struct Z80* z80);
void retCond(struct Z80* z80, u8 cond);
void jp(struct Z80* z80);
void jpCond(struct Z80* z80, u8 cond);

//Logical
void xor(struct Z80* z80, u8 reg);
void or(struct Z80* z80, u8 reg);

//Shifts
void rlca(struct Z80* z80);

//Stack
void push(struct Z80* z80, union Register* reg);
void pop(struct Z80* z80, union Register* reg);

//Io/Ports
void outa(struct Z80* z80);
void ina(struct Z80* z80);
void out(struct Z80* z80, u8 destPort, u8 sourceReg);
void in(struct Z80* z80, u8 sourcePort, u8* destReg, u8 opcode);

void otir(struct Z80* z80);
void ldir(struct Z80* z80);

//Interrupt related instructions
void di(struct Z80* z80);
void ei(struct Z80* z80);
void im(struct Z80* z80, enum IntMode interruptMode);
