#include "Z80.h"
#include "Bus.h"


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
		case 0xF5: push(z80, &z80->af); break;
		case 0xF3: di(z80); break;
		case 0xFB: ei(z80); break;
	}
}

void executeBitInstruction(struct Z80* z80, u8 opcode)
{
}

void executeIxInstruction(struct Z80* z80, u8 opcode)
{
}

void executeIxBitInstruction(struct Z80* z80, u8 opcode)
{
}

void executeExtendedInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		case 0x56: im(z80, One); break;
		case 0x76: im(z80, One); break;
	}
}

void executeIyInstruction(struct Z80* z80, u8 opcode)
{
}

void executeIyBitInstruction(struct Z80* z80, u8 opcode)
{
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

void push(struct Z80* z80, union Register* reg)
{
	z80->sp--;
	z80WriteU8(z80->af.hi, z80->sp);
	z80->sp--;
	z80WriteU8(z80->af.lo, z80->sp);

	z80->cycles = 11;
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
