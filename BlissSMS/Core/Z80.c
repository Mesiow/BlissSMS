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

	z80->cycles = 0;
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
}

void executeIyInstruction(struct Z80* z80, u8 opcode)
{
}

void executeIyBitInstruction(struct Z80* z80, u8 opcode)
{
}
