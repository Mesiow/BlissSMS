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
	z80->interrupts = 0;
}

void z80ConnectBus(struct Bus* bus)
{
	memBus = bus;
}

void z80ConnectIo(struct Io* io)
{
	ioBus = io;
}

void z80SetFlag(struct Z80* z80, u8 flags)
{
	z80->af.lo |= (flags & 0b1101'0111); //just mask out the copy bits they're useless
}

void z80SetFlagCond(struct Z80* z80, u8 cond, u8 flags)
{
	if (cond) {
		z80->af.lo |= (flags & 0b1101'0111);
	}
}

void z80ClearFlag(struct Z80* z80, u8 flags)
{
	z80->af.lo &= ~(flags & 0b1101'0111);
}

u8 z80OverflowFromAdd(u8 op1, u8 op2)
{
	return 0;
}

u8 z80OverflowFromSub(u8 op1, u8 op2)
{
	return 0;
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
	u8 opcode = z80ReadU8(z80->pc);
	z80->pc++;

	executeInstruction(z80, opcode);

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
				u8 ixbit_opcode = z80ReadU8(z80->pc++);
				executeIxBitInstruction(z80, ixbit_opcode);
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
		case 0x01: loadReg16(z80, &z80->bc); break;
		case 0x31: loadReg16(z80, &z80->sp); break;
		case 0x18: jrImm(z80); break;

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
			assert(0);
			break;
	}
}

void executeBitInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		default:
			printf("--Unimplemented Bit Instruction--: 0x%02X\n", opcode);
			assert(0);
			break;
	}
}

void executeIxInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
	default:
		printf("--Unimplemented Ix Instruction--: 0x%02X\n", opcode);
		assert(0);
		break;
	}
}

void executeIxBitInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
	default:
		printf("--Unimplemented Ix Bit Instruction--: 0x%02X\n", opcode);
		assert(0);
		break;
	}
}

void executeExtendedInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		case 0x40: in(z80, z80->bc.lo, &z80->bc.hi); break;
		case 0x48: in(z80, z80->bc.lo, &z80->bc.lo); break;
		case 0x50: in(z80, z80->bc.lo, &z80->de.hi); break;
		case 0x58: in(z80, z80->bc.lo, &z80->de.lo); break;
		case 0x60: in(z80, z80->bc.lo, &z80->hl.hi); break;
		case 0x68: in(z80, z80->bc.lo, &z80->hl.lo); break;
		case 0x70: in(z80, z80->bc.lo, NULL); break;
		case 0x78: in(z80, z80->bc.lo, &z80->af.hi); break;

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

		default:
			printf("--Unimplemented Extended Instruction--: 0x%02X\n", opcode);
			assert(0);
			break;
	}
}

void executeIyInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
	default:
		printf("--Unimplemented Iy Instruction--: 0x%02X\n", opcode);
		assert(0);
		break;
	}
}

void executeIyBitInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
	default:
		printf("--Unimplemented Iy Bit Instruction--: 0x%02X\n", opcode);
		assert(0);
		break;
	}
}

void loadReg16(struct Z80 *z80, union Register *reg)
{
	reg->value = z80FetchU16(z80);
	z80->cycles = 10;
}

void jrImm(struct Z80* z80)
{
	s8 imm = (s8)z80FetchU8(z80);
	z80->pc += imm;
	z80->cycles = 12;
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

void push(struct Z80* z80, union Register* reg)
{
	z80->sp--;
	z80WriteU8(z80->af.hi, z80->sp);
	z80->sp--;
	z80WriteU8(z80->af.lo, z80->sp);

	z80->cycles = 11;
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
	u8 io_value = ioReadU8(ioBus, io_port);
	z80->af.hi = io_value;

	z80->cycles = 11;
}

void out(struct Z80* z80, u8 destPort, u8 sourceReg)
{
	ioWriteU8(ioBus, destPort, sourceReg);
	z80->cycles = 12;
}

void in(struct Z80* z80, u8 sourcePort, u8* destReg)
{
	u8 io_value = ioReadU8(ioBus, sourcePort);

	if(destReg != NULL)
		*destReg = io_value;

	z80ClearFlag(z80, (FLAG_H | FLAG_N));

	(io_value == 0x0) ? z80SetFlag(z80, FLAG_Z) : z80ClearFlag(z80, FLAG_Z);
	(z80IsEvenParity(io_value)) ? z80SetFlag(z80, FLAG_PV) : z80ClearFlag(z80, FLAG_PV);
    (z80IsSigned(io_value)) ? z80SetFlag(z80, FLAG_S) : z80ClearFlag(z80, FLAG_S);

	z80->cycles = 12;
}

void di(struct Z80* z80)
{
	z80->interrupts = 0;
	z80->cycles = 4;
}

void ei(struct Z80* z80)
{
	z80->interrupts = 1;
	z80->cycles = 4;
}

void im(struct Z80* z80, enum IntMode interruptMode)
{
	z80->interrupt_mode = interruptMode;
	z80->cycles = 8;
}
