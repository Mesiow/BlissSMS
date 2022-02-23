#include "Z80.h"
#include "Bus.h"
#include "Io.h"


void z80Init(struct Z80* z80)
{
	z80->shadowedregs.af.value = 0x0;
	z80->shadowedregs.bc.value = 0x0;
	z80->shadowedregs.de.value = 0x0;
	z80->shadowedregs.hl.value = 0x0;

	z80->af.value = 0x0;
	z80->bc.value = 0x0;
	z80->de.value = 0x0;
	z80->hl.value = 0x0;
	z80->ix.value = 0x0;
	z80->iy.value = 0x0;
	z80->ir.value = 0x0;
	z80->sp = 0x0;
	z80->pc = 0x0;

	z80->interrupt_mode = Zero;
	z80->cycles = 0;
	z80->iff1 = 0;
	z80->iff2 = 0;
	z80->process_interrupt_delay = 0;
	z80->halted = 0;
}

void z80ConnectBus(struct Bus* bus)
{
	memBus = bus;
}

void z80ConnectIo(struct Io* io)
{
	ioBus = io;
}

void z80AffectFlag(struct Z80* z80, u8 cond, u8 flags)
{
	if (cond) {
		z80->af.lo |= (flags & 0b1101'0111);
	}
	else {
		z80->af.lo &= ~(flags & 0b1101'0111);
	}
}

void z80SetFlag(struct Z80* z80, u8 flags)
{
	z80->af.lo |= (flags & 0b1101'0111);
}

void z80ClearFlag(struct Z80* z80, u8 flags)
{
	z80->af.lo &= ~(flags & 0b1101'0111);
}

u8 getFlag(struct Z80* z80, u8 flag)
{
	u8 mask = (z80->af.lo & flag);
	return (mask ? 1 : 0);
}

void z80HandleInterrupts(struct Z80* z80)
{
	//Maskable interrupts enabled
	if (z80->iff1 & z80->iff2) {
		if (z80->interrupt_mode == One) {
			z80->halted = 0;
			//Save current pc to the stack
			z80->sp--;
			z80WriteU8((z80->pc >> 8) & 0xFF, z80->sp);
			z80->sp--;
			z80WriteU8(z80->pc & 0xFF, z80->sp);

			//disable interrupts and jump to routine
			z80->iff1 = z80->iff2 = 0;
			z80->pc = INT_VECTOR;
		}
	}
}

void z80HandleNonMaskableInterrupt(struct Z80* z80)
{
	z80->halted = 0; //exit halted state

	z80->iff2 = z80->iff1;
	z80->iff1 = 0;
	z80->pc = NMI_VECTOR;
}

u8 z80OverflowFromAdd(u8 op1, u8 op2)
{
	u8 total = (op1 + op2) & 0xFF;
	//check if sign are same and if they are ~(op1 ^ op2) will evaluate to 1
	//then check results sign and then and them together and determine overflow by the msb
	return (~(op1 ^ op2) & ((op1 ^ total))) >> 7;
}

u8 z80OverflowFromSub(u8 op1, u8 op2)
{		
	u8 total = (op1 - op2) & 0xFF;
	//first check if both operands have a different sign,
	//then check if the result has the same sign as the second operand
	return (((s8)(op1 ^ op2) < 0) && ((s8)(op2 ^ total) >= 0));
}

u8 z80IsEvenParity(u8 value)
{
	u8 count = popcount(value);
	return ((count % 2) == 0);
}

u8 z80IsSigned(u8 value)
{
	s8 signed_value = (s8)value;
	return (((signed_value >> 7) & 0x1) == 0x1);
}

u8 z80CarryOccured(u8 op1, u8 op2)
{
	return (((op1 & 0xFF) + (op2 & 0xFF)) > 0xFF);
}

u8 z80HalfCarryOccured(u8 op1, u8 op2)
{
	return ((op1 & 0xF) + (op2 & 0xF) > 0xF);
}

u8 z80BorrowOccured(u8 op1, u8 op2)
{
	return (op2 > op2);
}

u8 z80HalfBorrowOccured(u8 op1, u8 op2)
{
	return (((op1 & 0xF) - (op2 & 0xF)) < 0);
}

void z80WriteU8(u8 value, u16 address)
{
	memoryBusWriteU8(memBus, value, address);
}

u8 z80ReadU8(u16 address)
{
	return memoryBusReadU8(memBus, address);
}

u8 z80FetchU8(struct Z80* z80)
{
	u8 data = memoryBusReadU8(memBus, z80->pc++);
	return data;
}

u16 z80ReadU16(u16 address)
{
	u8 lo = memoryBusReadU8(memBus, address);
	u8 hi = memoryBusReadU8(memBus, address + 1);

	return ((hi << 8) | lo);
}

u16 z80FetchU16(struct Z80* z80)
{
	u8 lo = memoryBusReadU8(memBus, z80->pc++);
	u8 hi = memoryBusReadU8(memBus, z80->pc++);

	return ((hi << 8) | lo);
}

u16 z80Clock(struct Z80* z80)
{
	if (!z80->halted) {
		if (z80->process_interrupt_delay)
			z80->process_interrupt_delay = 0;

		u8 opcode = z80ReadU8(z80->pc);
		z80->pc++;

		executeInstruction(z80, opcode);
	}
	else //execute nops
		z80->cycles = 4;

	return z80->cycles;
}

void executeInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		case 0xCB: {
			u8 bit_opcode = z80ReadU8(z80->pc++);
			executeBitInstruction(z80, bit_opcode);
		}
		break;
		case 0xDD: {
			u8 ix_opcode = z80ReadU8(z80->pc++);
			if (ix_opcode == 0xCB) {
				//the opcode is the last byte in the instruction
				//and the immediate byte to fetch is the second to last
				u8 ixbit_opcode = z80ReadU8(z80->pc + 1);

				executeIxBitInstruction(z80, ixbit_opcode);
				z80->pc += 1;
			}
			else
				executeIxInstruction(z80, ix_opcode);
		}
		break;
		case 0xED: {
			u8 ext_opcode = z80ReadU8(z80->pc++);
			executeExtendedInstruction(z80, ext_opcode);
		}
		break;
		case 0xFD: {
			u8 iy_opcode = z80ReadU8(z80->pc++);
			if (iy_opcode == 0xCB) {
				u8 iybit_opcode = z80ReadU8(z80->pc++);
				executeIyBitInstruction(z80, iybit_opcode);
			}
			else
				executeIyInstruction(z80, iy_opcode);
		}
		break;

		default:
			executeMainInstruction(z80, opcode);
			break;
	}
}

void executeMainInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		case 0x00: z80->cycles = 4; break;

		//Load immediate u16 into register
		case 0x01: loadReg16(z80, &z80->bc); break;
		case 0x11: loadReg16(z80, &z80->de); break;
		case 0x21: loadReg16(z80, &z80->hl); break;
		case 0x31: loadReg16(z80, &z80->sp); break;

		//Misc
		case 0x2F: cpl(z80); break;
		case 0x76: halt(z80); break;
		case 0xFE: cp(z80); break;
		case 0x27: daa(z80); break;

		//Shifts
		case 0x07: rlca(z80); break;
		case 0x0F: rrca(z80); break;

		//Exchanges
		case 0x08: ex(z80, &z80->af, &z80->shadowedregs.af); break;
		case 0xEB: ex(z80, &z80->de, &z80->hl); break;
		case 0xD9: exx(z80); break;

		//Loads
		case 0x22: loadMemReg16(z80, &z80->hl); break;
		case 0x32: loadMemReg8(z80, z80->af.hi); break;

		case 0x06: loadReg8(z80, &z80->bc.hi); break;
		case 0x0E: loadReg8(z80, &z80->bc.lo); break;
		case 0x16: loadReg8(z80, &z80->de.hi); break;
		case 0x1E: loadReg8(z80, &z80->de.lo); break;
		case 0x26: loadReg8(z80, &z80->hl.hi); break;
		case 0x2E: loadReg8(z80, &z80->hl.lo); break;
		case 0x3E: loadReg8(z80, &z80->af.hi); break;
		case 0x36: loadHL8(z80); break;

		case 0x2A: load16Reg(z80, &z80->hl); break;
		case 0x3A: load16A(z80); break;

		case 0x4F: loadReg(z80, &z80->bc.lo, z80->af.hi); break;

		case 0x78: loadAReg(z80, z80->bc.hi); break;
		case 0x79: loadAReg(z80, z80->bc.lo); break;
		case 0x7A: loadAReg(z80, z80->de.hi); break;
		case 0x7B: loadAReg(z80, z80->de.lo); break;
		case 0x7C: loadAReg(z80, z80->hl.hi); break;
		case 0x7D: loadAReg(z80, z80->hl.lo); break;

		case 0x46: loadRegHL(z80, &z80->bc.hi); break;
		case 0x4E: loadRegHL(z80, &z80->bc.lo); break;
		case 0x56: loadRegHL(z80, &z80->de.hi); break;
		case 0x5E: loadRegHL(z80, &z80->de.lo); break;
		case 0x66: loadRegHL(z80, &z80->hl.hi); break;
		case 0x6E: loadRegHL(z80, &z80->hl.lo); break;
		case 0x7E: loadRegHL(z80, &z80->af.hi); break;

		case 0x70: loadHlReg(z80, z80->bc.hi); break;
		case 0x71: loadHlReg(z80, z80->bc.lo); break;
		case 0x72: loadHlReg(z80, z80->de.hi); break;
		case 0x73: loadHlReg(z80, z80->de.lo); break;
		case 0x74: loadHlReg(z80, z80->hl.hi); break;
		case 0x75: loadHlReg(z80, z80->hl.lo); break;
		case 0x77: loadHlReg(z80, z80->af.hi); break;

		//Arithmetic
		case 0x23: incReg16(z80, &z80->hl); break;
		case 0x0B: decReg16(z80, &z80->bc); break;
		case 0x1B: decReg16(z80, &z80->de); break;
		case 0x2B: decReg16(z80, &z80->hl); break;
		case 0x3B: decReg16(z80, &z80->sp); break;

		//Jumps/Branches/Rets
		case 0x10: djnz(z80); break;
		case 0x18: jrImm(z80); break;
		case 0x20: jrImmCond(z80, getFlag(z80, FLAG_Z) == 0); break;
		case 0x28: jrImmCond(z80, getFlag(z80, FLAG_Z)); break;

		case 0xA0: and(z80, z80->bc.hi); break;
		case 0xA1: and(z80, z80->bc.lo); break;
		case 0xA2: and(z80, z80->de.hi); break;
		case 0xA3: and(z80, z80->de.lo); break;
		case 0xA4: and(z80, z80->hl.hi); break;
		case 0xA5: and(z80, z80->hl.lo); break;
		case 0xA6: andMemHl(z80); break;
		case 0xA7: and(z80, z80->af.hi); break;

		case 0xA8: xor(z80, z80->bc.hi); break;
		case 0xA9: xor(z80, z80->bc.lo); break;
		case 0xAA: xor(z80, z80->de.hi); break;
		case 0xAB: xor(z80, z80->de.lo); break;
		case 0xAC: xor(z80, z80->hl.hi); break;
		case 0xAD: xor(z80, z80->hl.lo); break;
		case 0xAE: xorMemHl(z80); break;
		case 0xAF: xor(z80, z80->af.hi); break;

		case 0xB0: or(z80, z80->bc.hi); break;
		case 0xB1: or(z80, z80->bc.lo); break;
		case 0xB2: or(z80, z80->de.hi); break;
		case 0xB3: or(z80, z80->de.lo); break;
		case 0xB4: or(z80, z80->hl.hi); break;
		case 0xB5: or(z80, z80->hl.lo); break;
		case 0xB6: orMemHl(z80); break;
		case 0xB7: or(z80, z80->af.hi); break;


		case 0xCD: call(z80); break;
		case 0xDC: callCond(z80, getFlag(z80, FLAG_C)); break;

		case 0xC9: ret(z80); break;

		case 0xC3: jp(z80); break;
		case 0xCA: jpCond(z80, getFlag(z80, FLAG_Z)); break;
		case 0xD2: jpCond(z80, getFlag(z80, FLAG_C) == 0); break;
		case 0xDA: jpCond(z80, getFlag(z80, FLAG_C)); break;

		case 0xC7: rst(z80, 0x00); break;
		case 0xCF: rst(z80, 0x08); break;
		case 0xD7: rst(z80, 0x10); break;
		case 0xDF: rst(z80, 0x18); break;
		case 0xE7: rst(z80, 0x20); break;
		case 0xEF: rst(z80, 0x28); break;
		case 0xF7: rst(z80, 0x30); break;
		case 0xFF: rst(z80, 0x38); break;

		case 0xD3: outa(z80); break;
		case 0xDB: ina(z80); break;

		case 0xC5: push(z80, &z80->bc); break;
		case 0xD5: push(z80, &z80->de); break;
		case 0xE5: push(z80, &z80->hl); break;
		case 0xF5: push(z80, &z80->af); break;

		case 0xC1: pop(z80, &z80->bc); break;
		case 0xD1: pop(z80, &z80->de); break;
		case 0xE1: pop(z80, &z80->hl); break;
		case 0xF1: pop(z80, &z80->af); break;

		case 0xF3: di(z80); break;
		case 0xFB: ei(z80); break;

		default:
			printf("--Unimplemented Main Instruction--: 0x%02X\n", opcode);
			printf("PC: 0x%04X\n", z80->pc);
			assert(0);
			break;
	}
}

void executeBitInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		default:
			printf("--Unimplemented Bit Instruction--: 0x%02X\n", opcode);
			printf("PC: 0x%04X\n", z80->pc);
			assert(0);
			break;
	}
}

void executeIxInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		case 0x21: loadReg16(z80, &z80->ix); z80->cycles += 4; break;
		case 0xE5: push(z80, &z80->ix); break;
		default:
			printf("--Unimplemented Ix Instruction--: 0x%02X\n", opcode);
			printf("PC: 0x%04X\n", z80->pc);
			assert(0);
			break;
	}
}

void executeIxBitInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
	case 0x7E: bitIx(z80, 7); break;
	default:
		printf("--Unimplemented Ix Bit Instruction--: 0x%02X\n", opcode);
		printf("PC: 0x%04X\n", z80->pc);
		assert(0);
		break;
	}
}

void executeExtendedInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		case 0x40: in(z80, z80->bc.lo, &z80->bc.hi, opcode); break;
		case 0x48: in(z80, z80->bc.lo, &z80->bc.lo, opcode); break;
		case 0x50: in(z80, z80->bc.lo, &z80->de.hi, opcode); break;
		case 0x58: in(z80, z80->bc.lo, &z80->de.lo, opcode); break;
		case 0x60: in(z80, z80->bc.lo, &z80->hl.hi, opcode); break;
		case 0x68: in(z80, z80->bc.lo, &z80->hl.lo, opcode); break;
		case 0x70: in(z80, z80->bc.lo, NULL, opcode); break;
		case 0x78: in(z80, z80->bc.lo, &z80->af.hi, opcode); break;

		case 0x41: out(z80, z80->bc.lo, z80->bc.hi); break;
		case 0x49: out(z80, z80->bc.lo, z80->bc.lo); break;
		case 0x51: out(z80, z80->bc.lo, z80->de.hi); break;
		case 0x59: out(z80, z80->bc.lo, z80->de.lo); break;
		case 0x61: out(z80, z80->bc.lo, z80->hl.hi); break;
		case 0x69: out(z80, z80->bc.lo, z80->hl.lo); break;
		case 0x71: out(z80, z80->bc.lo, 0); break;
		case 0x79: out(z80, z80->bc.lo, z80->af.hi); break;

		case 0x56: im(z80, One); break;
		case 0x76: im(z80, One); break;

		case 0xB0: ldir(z80); break;
		case 0xB3: otir(z80); break;

		default:
			printf("--Unimplemented Extended Instruction--: 0x%02X\n", opcode);
			printf("PC: 0x%04X\n", z80->pc);
			assert(0);
			break;
	}
}

void executeIyInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
	case 0xE5: push(z80, &z80->iy); break;
	default:
		printf("--Unimplemented Iy Instruction--: 0x%02X\n", opcode);
		printf("PC: 0x%04X\n", z80->pc);
		assert(0);
		break;
	}
}

void executeIyBitInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
	default:
		printf("--Unimplemented Iy Bit Instruction--: 0x%02X\n", opcode);
		printf("PC: 0x%04X\n", z80->pc);
		assert(0);
		break;
	}
}

void loadReg16(struct Z80 *z80, union Register *reg)
{
	reg->value = z80FetchU16(z80);
	z80->cycles = 10;
}

void loadReg8(struct Z80* z80, u8 *reg)
{
	*reg = z80FetchU8(z80);
	z80->cycles = 7;
}

void loadHL8(struct Z80* z80)
{
	u8 value = z80FetchU8(z80);
	z80WriteU8(value, z80->hl.value);
	z80->cycles = 10;
}

void loadAReg(struct Z80* z80, u8 reg)
{
	z80->af.hi = reg;
	z80->cycles = 4;
}

void loadRegHL(struct Z80* z80, u8* reg)
{
	*reg = z80ReadU8(z80->hl.value);
	z80->cycles = 7;
}

void loadMemReg16(struct Z80* z80, union Register* reg)
{
	u16 address = z80FetchU16(z80);
	z80WriteU8(reg->lo, address);
	z80WriteU8(reg->hi, address + 1);

	z80->cycles = 16;
}

void loadMemReg8(struct Z80* z80, u8 reg)
{
	u16 address = z80FetchU16(z80);
	z80WriteU8(reg, address);

	z80->cycles = 13;
}

void loadHlReg(struct Z80* z80, u8 reg)
{
	z80WriteU8(reg, z80->hl.value);
	z80->cycles = 7;
}

void load16Reg(struct Z80* z80, union Register* reg)
{
	u16 address = z80FetchU16(z80);
	
	u8 lo = z80ReadU8(address);
	u8 hi = z80ReadU8(address + 1);
	u16 value = ((hi << 8) | lo);

	reg->value = value;
	z80->cycles = 16;
}

void load16A(struct Z80* z80)
{
	u16 address = z80FetchU16(z80);
	u8 value = z80ReadU8(address);

	z80->af.hi = value;
	z80->cycles = 13;
}

void loadReg(struct Z80* z80, u8* destReg, u8 sourceReg)
{
	*destReg = sourceReg;
	z80->cycles = 4;
}

void incReg16(struct Z80* z80, union Register* reg)
{
	reg->value++;
	z80->cycles = 6;
}

void decReg16(struct Z80* z80, union Register* reg)
{
	reg->value--;
	z80->cycles = 6;
}

void decReg8(struct Z80* z80, u8* reg)
{

}

void jrImm(struct Z80* z80)
{
	s8 imm = (s8)z80FetchU8(z80);
	z80->pc += imm;
	z80->cycles = 12;
}

void jrImmCond(struct Z80* z80, u8 cond)
{
	if (cond) {
		jrImm(z80);
	}
	else {
		z80->pc += 1;
		z80->cycles = 7;
	}
}

void djnz(struct Z80* z80)
{
	z80->bc.hi--;
	if (z80->bc.hi != 0) {
		jrImm(z80);
		z80->cycles += 1;
	}
	else {
		z80->pc += 1;
		z80->cycles = 8;
	}
}

void rst(struct Z80* z80, u8 vector)
{
	z80->sp--;
	z80WriteU8((z80->pc >> 8) & 0xFF, z80->sp);
	z80->sp--;
	z80WriteU8((z80->pc & 0xFF), z80->sp);

	z80->pc = vector;
	z80->cycles = 11;
}

void call(struct Z80* z80)
{
	u16 address = z80FetchU16(z80);
	z80->sp--;
	z80WriteU8((z80->pc >> 8) & 0xFF, z80->sp);
	z80->sp--;
	z80WriteU8((z80->pc & 0xFF), z80->sp);

	z80->pc = address;
	z80->cycles = 17;
}

void callCond(struct Z80* z80, u8 cond)
{
	if (cond){
		call(z80);
	}
	else {
		z80->pc += 2;
		z80->cycles = 10;
	}
}

void ret(struct Z80* z80)
{
	u8 lo = z80ReadU8(z80->sp);
	z80->sp++;
	u8 hi = z80ReadU8(z80->sp);
	z80->sp++;

	u16 return_address = ((hi << 8) | lo);
	z80->pc = return_address;
	z80->cycles = 10;
}

void retCond(struct Z80* z80, u8 cond)
{
	if (cond) {
		ret(z80);
		z80->cycles += 1;
	}
	else {
		z80->cycles = 5;
	}
}

void jp(struct Z80* z80)
{
	u16 address = z80FetchU16(z80);
	z80->pc = address;
	z80->cycles = 10;
}

void jpCond(struct Z80* z80, u8 cond)
{
	if (cond) {
		jp(z80);
	}
	else
		z80->cycles = 10;
}

void xor(struct Z80* z80, u8 reg)
{
	u8 result = z80->af.hi ^ reg;
	z80->af.hi ^= reg;

	z80AffectFlag(z80, (result == 0), FLAG_Z);
	z80AffectFlag(z80, z80IsEvenParity(result), FLAG_PV);
	z80AffectFlag(z80, z80IsSigned(result), FLAG_S);

	z80ClearFlag(z80, (FLAG_C | FLAG_N | FLAG_H));
	z80->cycles = 4;
}

void xorMemHl(struct Z80* z80)
{
	u8 value = z80ReadU8(z80->hl.value);
	xor(z80, value);

	z80->cycles += 3;
}

void or(struct Z80 * z80, u8 reg)
{
	u8 result = z80->af.hi | reg;
	z80->af.hi |= reg;

	z80AffectFlag(z80, (result == 0), FLAG_Z);
	z80AffectFlag(z80, z80IsEvenParity(result), FLAG_PV);
	z80AffectFlag(z80, z80IsSigned(result), FLAG_S);

	z80ClearFlag(z80, (FLAG_C | FLAG_N | FLAG_H));
	z80->cycles = 4;
}

void orMemHl(struct Z80* z80)
{
	u8 value = z80ReadU8(z80->hl.value);
	or(z80, value);

	z80->cycles += 3;
}

void and(struct Z80* z80, u8 reg)
{
	u8 result = z80->af.hi & reg;
	z80->af.hi &= reg;

	z80AffectFlag(z80, (result == 0), FLAG_Z);
	z80AffectFlag(z80, z80IsEvenParity(result), FLAG_PV);
	z80AffectFlag(z80, z80IsSigned(result), FLAG_S);

	z80SetFlag(z80, FLAG_H);
	z80ClearFlag(z80, (FLAG_C | FLAG_N));
	z80->cycles = 4;
}

void andMemHl(struct Z80* z80)
{
	u8 value = z80ReadU8(z80->hl.value);
	and(z80, value);

	z80->cycles += 3;
}

void rlca(struct Z80* z80)
{
	u8 msb = (z80->af.hi >> 7) & 0x1;
	z80ClearFlag(z80, (FLAG_N | FLAG_H));

	z80->af.hi <<= 1;

	//Old bit 7 stored to carry, old bit 7 moved to it 0
	if (msb) {
		z80SetFlag(z80, FLAG_C);
		z80->af.hi = setBit(z80->af.hi, 0);
	}
	else {
		z80ClearFlag(z80, FLAG_C);
		z80->af.hi = clearBit(z80->af.hi, 0);
	}

	z80->cycles = 4;
}

void rrca(struct Z80* z80)
{
	u8 lsb = (z80->af.hi & 0x1);
	z80ClearFlag(z80, (FLAG_N | FLAG_H));

	z80->af.hi >>= 1;

	//Old bit 0 stored to carry, old bit 0 moved to bit 7
	if (lsb) {
		z80SetFlag(z80, FLAG_C);
		z80->af.hi = setBit(z80->af.hi, 7);
	}
	else {
		z80ClearFlag(z80, FLAG_C);
		z80->af.hi = clearBit(z80->af.hi, 7);
	}

	z80->cycles = 4;
}

void ex(struct Z80* z80, union Register* reg1, union Register* reg2)
{
	u16 temp_reg1 = reg1->value;
	reg1->value = reg2->value;
	reg2->value = temp_reg1;

	z80->cycles = 4;
}

void exx(struct Z80* z80)
{
	u16 temp_bc = z80->bc.value;
	z80->bc.value = z80->shadowedregs.bc.value;
	z80->shadowedregs.bc.value = temp_bc;

	u16 temp_de = z80->de.value;
	z80->de.value = z80->shadowedregs.de.value;
	z80->shadowedregs.de.value = temp_de;

	u16 temp_hl = z80->hl.value;
	z80->hl.value = z80->shadowedregs.hl.value;
	z80->shadowedregs.hl.value = temp_hl;

	z80->cycles = 4;
}

void push(struct Z80* z80, union Register* reg)
{
	z80->sp--;
	z80WriteU8(reg->hi, z80->sp);
	z80->sp--;
	z80WriteU8(reg->lo, z80->sp);

	z80->cycles = 11;
	if (reg == &z80->ix || reg == &z80->iy)
		z80->cycles += 4;
}

void pop(struct Z80* z80, union Register* reg)
{
	reg->lo = z80ReadU8(z80->sp);
	z80->sp++;
	reg->hi = z80ReadU8(z80->sp);
	z80->sp++;

	z80->cycles = 10;
}

void outa(struct Z80* z80)
{
	u8 io_port = z80FetchU8(z80);
	ioWriteU8(ioBus, z80->af.hi, io_port);

	z80->cycles = 11;
}

void ina(struct Z80* z80)
{
	u8 io_port = z80FetchU8(z80);

	u8 open_bus = ((io_port >= 0x0) && (io_port <= 0x3F));
	if (open_bus) {
		z80->af.hi = io_port;
	}
	else {
		u8 io_value = ioReadU8(ioBus, io_port);
		z80->af.hi = io_value;
	}

	z80->cycles = 11;
}

void out(struct Z80* z80, u8 destPort, u8 sourceReg)
{
	ioWriteU8(ioBus, destPort, sourceReg);
	z80->cycles = 12;
}

void in(struct Z80* z80, u8 sourcePort, u8* destReg, u8 opcode)
{
	u8 io_value = 0x0;
	u8 open_bus = ((sourcePort >= 0x0) && (sourcePort <= 0x3F));
	if (open_bus) {
		io_value = opcode;
	}else
		io_value = ioReadU8(ioBus, sourcePort);

	if(destReg != NULL)
		*destReg = io_value;

	z80AffectFlag(z80, (io_value == 0), FLAG_Z);
	z80AffectFlag(z80, z80IsEvenParity(io_value), FLAG_PV);
	z80AffectFlag(z80, z80IsSigned(io_value), FLAG_S);

	z80ClearFlag(z80, (FLAG_H | FLAG_N));
	z80->cycles = 12;
}

void otir(struct Z80* z80)
{
	z80->cycles = ((z80->bc.hi != 0) ? 21 : 16);
	
	if (z80->bc.hi != 0) {
		//Byte from address hl written to port c
		u8 value = z80ReadU8(z80->hl.value);
		u8 io_port = z80->bc.lo;
		ioWriteU8(ioBus, value, io_port);

		z80->hl.value++;
		z80->bc.hi--;

		//If byte counter (B) is not 0, pc is decremented by 2 and the instruction
		//will be repeated (also interrupts will be checked and possibly triggered
		//until the instructions terminates once B becomes 0
		if (z80->bc.hi != 0)
			z80->pc -= 2;
	}
	z80SetFlag(z80, (FLAG_Z | FLAG_N));
}

void ldir(struct Z80* z80)
{
	z80->cycles = ((z80->bc.value != 0) ? 21 : 16);

	if (z80->bc.value != 0) {
		//Transfers bytes from source address hl to destination 
		//address de, bc times
		u8 value = z80ReadU8(z80->hl.value);
		z80WriteU8(value, z80->de.value);

		z80->hl.value++;
		z80->de.value++;
		z80->bc.value--;

		if (z80->bc.value != 0)
			z80->pc -= 2;
	}
	z80ClearFlag(z80, (FLAG_N | FLAG_PV | FLAG_H));
}

void cpl(struct Z80* z80)
{
	z80->af.hi = ~(z80->af.hi);

	z80SetFlag(z80, (FLAG_N | FLAG_H));
	z80->cycles = 4;
}

void halt(struct Z80* z80)
{
	z80->halted = 1;
	z80->cycles = 4;
}

void cp(struct Z80* z80)
{
	u8 value = z80FetchU8(z80);
	u8 result = z80->af.hi - value;

	z80SetFlag(z80, FLAG_N);
	z80AffectFlag(z80, result == 0, FLAG_Z);
	z80AffectFlag(z80, z80OverflowFromSub(z80->af.hi, value), FLAG_PV);
	z80AffectFlag(z80, z80IsSigned(result), FLAG_S);
	z80AffectFlag(z80, z80BorrowOccured(z80->af.hi, value), FLAG_C);
	z80AffectFlag(z80, z80HalfBorrowOccured(z80->af.hi, value), FLAG_H);

	z80->cycles = 7;
}

void daa(struct Z80* z80)
{
	/*

			The DA instruction adjusts the 8-bit value in the accumulator
			to correspond to binary-coded decimal (BCD) format.
			This instruction begins by testing the low-order nibble of the
			accumulator. If the AC flag is set or if the low 4 bits of the
			accumulator exceed a value of 9, the accumulator is incremented
			by 6. The high-order nibble is then tested.
			If the carry flag is set or if the high 4 bits of the accumulator
			exceed a value of 9, the value 60h is added to the accumulator.
			This instruction performs a decimal conversion by adding 00h,
			06h, or 66h to the accumulator depending on the initial contents
			of the PSW and accumulator.

		*/
	s32 a = z80->af.hi;

	if (!getFlag(z80, FLAG_N)) {
		if ((getFlag(z80, FLAG_H)) || (a & 0x0F) > 9)
			a += 6;

		if ((getFlag(z80, FLAG_C)) || ((a >> 4) & 0xF) > 9)
			a += 0x60;

		z80->last_daa_operation = 0;
	}
	else {
		if (getFlag(z80, FLAG_H)) {
			a -= 6;
			if (!(getFlag(z80, FLAG_C)))
				a &= 0xFF;
		}
		if (getFlag(z80, FLAG_C))
			a -= 0x60;

		z80->last_daa_operation = 1;
	}

	z80ClearFlag(z80, (FLAG_H | FLAG_Z));
	if (a & 0x100)
		z80SetFlag(z80, FLAG_C);

	z80->af.hi = a & 0xFF;

	if (!z80->af.hi)
		z80SetFlag(z80, FLAG_Z);

	z80AffectFlag(z80, z80IsEvenParity(z80->af.hi), FLAG_PV);

	z80->cycles = 4;
}

void bitIx(struct Z80* z80, u8 bit)
{
	u8 offset = z80FetchU8(z80);
	u8 value = z80ReadU8(z80->ix.value + offset);

	z80AffectFlag(z80, testBit(value, bit) == 0, FLAG_Z);
	z80ClearFlag(z80, FLAG_N);
	z80SetFlag(z80, FLAG_H);

	z80->cycles = 20;
}

void di(struct Z80* z80)
{
	z80->iff1 = 0;
	z80->iff2 = 0;
	z80->process_interrupt_delay = 0;
	z80->cycles = 4;
}

void ei(struct Z80* z80)
{
	z80->iff1 = 1;
	z80->iff2 = 1;
	z80->process_interrupt_delay = 1;
	z80->cycles = 4;
}

void im(struct Z80* z80, enum IntMode interruptMode)
{
	z80->interrupt_mode = interruptMode;
	z80->cycles = 8;
}
