#include "Z80.h"
#include "Bus.h"
#include "Io.h"
#include "Vdp.h"


void cpmLoadRom(struct Z80* z80, const char *path)
{
	FILE* rom = fopen(path, "rb");
	if (rom == NULL) {
		printf("---CP/M file could not be found---\n");
		return;
	}
	//Rom size
	fseek(rom, 0, SEEK_END);
	u32 file_size = ftell(rom);
	fseek(rom, 0, SEEK_SET);
	printf("file size: %d\n", file_size);

	u8* temp = (u8*)malloc(file_size * sizeof(u8));
	if (temp != NULL) {
		fread(temp, sizeof(u8), file_size, rom);
		for (s32 i = 0; i < file_size; i++) {
			z80->cpm.memory[i + 0x100] = temp[i];
		}
		free(temp);
		printf("Test %s loaded.\n", path);
	}
	fclose(rom);
}

void cpmHandleSysCalls(struct Z80* z80)
{
	if (z80->pc == 0x5) {
		if (z80->bc.lo == 0x2) {
			printf("%c", z80->de.lo);
		}
		else if (z80->bc.lo == 0x9) {
			u16 i = z80->de.value;
			while (cpmReadMem8(z80, i) != '$') {
				printf("%c", cpmReadMem8(z80, i));
				i++;
			}
		}
	}
}

void cpmWriteMem8(struct Z80* z80, u16 address, u8 value)
{
	z80->cpm.memory[address] = value;
}

u8 cpmReadMem8(struct Z80* z80, u16 address)
{
	return z80->cpm.memory[address];
}

void z80DebugOutput(struct Z80* z80)
{
	printf("PC: %04X, AF: %04X, BC: %04X, DE: %04X, HL: %04X, SP: %04X, "
		"IX: %04X, IY: %04X, I: %02X, R: %02X\n",
		z80->pc, z80->af.value, z80->bc.value, z80->de.value, z80->hl.value, z80->sp,
		z80->ix.value, z80->iy.value, z80->ir.hi, z80->ir.lo);
}

void z80Init(struct Z80* z80)
{
	z80->cpm_stub_enabled = 1;

	if (z80->cpm_stub_enabled) {
		//Cpm stub for testing our z80 core
		memset(z80->cpm.memory, 0x0, 0x10000);
		// inject "out 1,a" at 0x0000 (signal to stop the test)
		z80->cpm.memory[0x0000] = 0xD3;
		z80->cpm.memory[0x0001] = 0x00;

		// inject "in a,0" at 0x0005 (signal to output some characters)
		z80->cpm.memory[0x0005] = 0xDB;
		z80->cpm.memory[0x0006] = 0x00;
		z80->cpm.memory[0x7] = 0xC9; //RET at 0x7
		z80->pc = 0x100;
		z80->af.value = 0xFFFF;
		z80->sp = 0xFFFF;
	}
	else {
		z80->pc = 0x0;
		z80->sp = 0xDFF0;
		z80->af.value = 0x0;
	}


	z80->shadowedregs.af.value = 0x0;
	z80->shadowedregs.bc.value = 0x0;
	z80->shadowedregs.de.value = 0x0;
	z80->shadowedregs.hl.value = 0x0;

	z80->bc.value = 0x0;
	z80->de.value = 0x0;
	z80->hl.value = 0x0;
	z80->ix.value = 0x0;
	z80->iy.value = 0x0;
	z80->ir.value = 0x0;

	z80->interrupt_mode = One;
	z80->cycles = 0;
	z80->iff1 = 0;
	z80->iff2 = 0;
	z80->process_interrupt_delay = 0;
	z80->halted = 0;
	z80->last_daa_operation = 0;
	z80->service_nmi = 0;
}

void z80ConnectBus(struct Z80* z80, struct Bus* bus)
{
	z80->bus = bus;
}

void z80ConnectIo(struct Z80* z80, struct Io* io)
{
	z80->io = io;
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

void z80HandleInterrupts(struct Z80* z80, struct Vdp *vdp)
{
	//nmi has priority over maskable irqs
	if (z80->service_nmi) {
		z80->service_nmi = 0;
		z80->halted = 0; //exit halted state

		z80->iff2 = z80->iff1;
		z80->iff1 = 0;
		rst(z80, NMI_VECTOR);
	}

	if (vdpPendingInterrupts(vdp)) {
		//Maskable interrupts enabled
		if (z80->iff1) {
			if (z80->interrupt_mode == One) {
				//disable interrupts and jump to routine
				z80->iff1 = z80->iff2 = 0;
				z80->halted = 0;
				rst(z80, INT_VECTOR);
			}
		}
	}
}

u8 z80OverflowFromAdd8(u8 op1, u8 op2)
{
	u8 total = (op1 + op2) & 0xFF;
	//check if sign are same and if they are ~(op1 ^ op2) will evaluate to 1
	//then check results sign and then and them together and determine overflow by the msb
	return (~(op1 ^ op2) & ((op1 ^ total))) >> 7;
}

u8 z80OverflowFromSub8(u8 op1, u8 op2)
{		
	u8 total = (op1 - op2) & 0xFF;
	//first check if both operands have a different sign,
	//then check if the result has the same sign as the second operand
	return (((s8)(op1 ^ op2) < 0) && ((s8)(op2 ^ total) >= 0));
}

u8 z80OverflowFromAdd16(u16 op1, u16 op2)
{
	u16 total = (op1 + op2) & 0xFFFF;
	return (~(op1 ^ op2) & ((op1 ^ total))) >> 15;
}

u8 z80OverflowFromSub16(u16 op1, u16 op2)
{
	u16 total = (op1 - op2) & 0xFFFF;
	return (((s16)(op1 ^ op2) < 0) && ((s16)(op2 ^ total) >= 0));
}

u8 z80IsEvenParity(u8 value)
{
	u8 count = popcount(value);
	return ((count % 2) == 0);
}

u8 z80IsSigned16(u16 value)
{
	s16 signed_value = (s16)value;
	return (((signed_value >> 15) & 0x1) == 0x1);
}

u8 z80IsSigned8(u8 value)
{
	s8 signed_value = (s8)value;
	return (((signed_value >> 7) & 0x1) == 0x1);
}

u8 z80CarryOccured8(u8 op1, u8 op2)
{
	return (((op1 & 0xFF) + (op2 & 0xFF)) > 0xFF);
}

u8 z80HalfCarryOccured8(u8 op1, u8 op2)
{
	return (((op1 & 0xF) + (op2 & 0xF)) > 0xF);
}

u8 z80BorrowOccured8(u8 op1, u8 op2)
{
	return (op2 > op1);
}

u8 z80HalfBorrowOccured8(u8 op1, u8 op2)
{
	return (((op1 & 0xF) - (op2 & 0xF)) < 0);
}

u8 z80CarryOccured16(u16 op1, u16 op2)
{
	return (((op1 & 0xFFFF) + (op2 & 0xFFFF)) > 0xFFFF);
}

u8 z80HalfCarryOccured16(u16 op1, u16 op2)
{
	return ((op1 & 0xFFF) + (op2 & 0xFFF) > 0xFFF);
}

u8 z80BorrowOccured16(u16 op1, u16 op2)
{
	return (op2 > op1);
}

u8 z80HalfBorrowOccured16(u16 op1, u16 op2)
{
	return (((op1 & 0xFFF) - (op2 & 0xFFF)) < 0);
}

void z80WriteU8(struct Z80* z80, u8 value, u16 address)
{
	if (z80->cpm_stub_enabled) {
		cpmWriteMem8(z80, address, value);
	}
	else
		memoryBusWriteU8(z80->bus, value, address);
}

u8 z80ReadU8(struct Z80* z80, u16 address)
{
	if (z80->cpm_stub_enabled) {
		return cpmReadMem8(z80, address);
	}
	return memoryBusReadU8(z80->bus, address);
}

u8 z80FetchU8(struct Z80* z80)
{
	if (z80->cpm_stub_enabled) {
		u8 data = cpmReadMem8(z80, z80->pc++);
		return data;
	}
	u8 data = memoryBusReadU8(z80->bus, z80->pc++);
	return data;
}

u16 z80ReadU16(struct Z80* z80, u16 address)
{
	if (z80->cpm_stub_enabled) {
		u8 lo = cpmReadMem8(z80, address);
		u8 hi = cpmReadMem8(z80, address + 1);

		return ((hi << 8) | lo);
	}
	u8 lo = memoryBusReadU8(z80->bus, address);
	u8 hi = memoryBusReadU8(z80->bus, address + 1);

	return ((hi << 8) | lo);
}

u16 z80FetchU16(struct Z80* z80)
{
	if (z80->cpm_stub_enabled) {
		u8 lo = cpmReadMem8(z80, z80->pc++);
		u8 hi = cpmReadMem8(z80, z80->pc++);

		return ((hi << 8) | lo);
	}
	u8 lo = memoryBusReadU8(z80->bus, z80->pc++);
	u8 hi = memoryBusReadU8(z80->bus, z80->pc++);

	return ((hi << 8) | lo);
}

u16 z80Clock(struct Z80* z80)
{
	if (!z80->halted) {
		if (z80->process_interrupt_delay)
			z80->process_interrupt_delay = 0;

		//Prelim test finished
		if (z80->cpm_stub_enabled) {
			cpmHandleSysCalls(z80);
			if (z80->pc == 0) {
				z80->halted = 1;
				return;
			}
		}
		u8 opcode = z80ReadU8(z80, z80->pc);
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
			u8 bit_opcode = z80ReadU8(z80, z80->pc++);
			executeBitInstruction(z80, bit_opcode);
		}
		break;
		case 0xDD: {
			u8 ix_opcode = z80ReadU8(z80, z80->pc++);
			if (ix_opcode == 0xCB) {
				//the opcode is the last byte in the instruction
				//and the immediate byte to fetch is the second to last
				u8 ixbit_opcode = z80ReadU8(z80, z80->pc + 1);

				executeIxBitInstruction(z80, ixbit_opcode);
				z80->pc += 1;
			}
			else
				executeIxInstruction(z80, ix_opcode);
		}
		break;
		case 0xED: {
			u8 ext_opcode = z80ReadU8(z80, z80->pc++);
			executeExtendedInstruction(z80, ext_opcode);
		}
		break;
		case 0xFD: {
			u8 iy_opcode = z80ReadU8(z80, z80->pc++);
			if (iy_opcode == 0xCB) {
				u8 iybit_opcode = z80ReadU8(z80, z80->pc + 1);

				executeIyBitInstruction(z80, iybit_opcode);
				z80->pc += 1;
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
		case 0xF9: loadSpReg(z80, &z80->hl); break;

		//Misc
		case 0x2F: cpl(z80); break;
		case 0x76: halt(z80); break;
		case 0xFE: cp(z80, z80FetchU8(z80)); z80->cycles += 3; break;
		case 0x27: daa(z80); break;
		case 0x3F: ccf(z80); break;
		case 0x37: scf(z80); break;

		//Shifts
		case 0x07: rlca(z80); break;
		case 0x0F: rrca(z80); break;
		case 0x17: rla(z80); break;
		case 0x1F: rra(z80); break;

		//Exchanges
		case 0x08: ex(z80, &z80->af, &z80->shadowedregs.af); break;
		case 0xEB: ex(z80, &z80->de, &z80->hl); break;
		case 0xD9: exx(z80); break;

		//Loads

		//Register pointer loads
		case 0x0A: loadRegMem(z80, &z80->af.hi, &z80->bc); break;
		case 0x1A: loadRegMem(z80, &z80->af.hi, &z80->de); break;
		case 0x02: loadReg8Mem(z80, z80->bc, z80->af.hi); break;
		case 0x12: loadReg8Mem(z80, z80->de, z80->af.hi); break;

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

		//Load src reg into dest reg
		case 0x40: loadReg(z80, &z80->bc.hi, z80->bc.hi); break;
		case 0x41: loadReg(z80, &z80->bc.hi, z80->bc.lo); break;
		case 0x42: loadReg(z80, &z80->bc.hi, z80->de.hi); break;
		case 0x43: loadReg(z80, &z80->bc.hi, z80->de.lo); break;
		case 0x44: loadReg(z80, &z80->bc.hi, z80->hl.hi); break;
		case 0x45: loadReg(z80, &z80->bc.hi, z80->hl.lo); break;
		case 0x47: loadReg(z80, &z80->bc.hi, z80->af.hi); break;
		case 0x48: loadReg(z80, &z80->bc.lo, z80->bc.hi); break;
		case 0x49: loadReg(z80, &z80->bc.lo, z80->bc.lo); break;
		case 0x4A: loadReg(z80, &z80->bc.lo, z80->de.hi); break;
		case 0x4B: loadReg(z80, &z80->bc.lo, z80->de.lo); break;
		case 0x4C: loadReg(z80, &z80->bc.lo, z80->hl.hi); break;
		case 0x4D: loadReg(z80, &z80->bc.lo, z80->hl.lo); break;
		case 0x4F: loadReg(z80, &z80->bc.lo, z80->af.hi); break;
		case 0x50: loadReg(z80, &z80->de.hi, z80->bc.hi); break;
		case 0x51: loadReg(z80, &z80->de.hi, z80->bc.lo); break;
		case 0x52: loadReg(z80, &z80->de.hi, z80->de.hi); break;
		case 0x53: loadReg(z80, &z80->de.hi, z80->de.lo); break;
		case 0x54: loadReg(z80, &z80->de.hi, z80->hl.hi); break;
		case 0x55: loadReg(z80, &z80->de.hi, z80->hl.lo); break;
		case 0x57: loadReg(z80, &z80->de.hi, z80->af.hi); break;
		case 0x5B: loadReg(z80, &z80->de.lo, z80->de.lo); break;
		case 0x5D: loadReg(z80, &z80->de.lo, z80->hl.lo); break;
		case 0x5F: loadReg(z80, &z80->de.lo, z80->af.hi); break;
		case 0x60: loadReg(z80, &z80->hl.hi, z80->bc.hi); break;
		case 0x61: loadReg(z80, &z80->hl.hi, z80->bc.lo); break;
		case 0x62: loadReg(z80, &z80->hl.hi, z80->de.hi); break;
		case 0x63: loadReg(z80, &z80->hl.hi, z80->de.lo); break;
		case 0x64: loadReg(z80, &z80->hl.hi, z80->hl.hi); break;
		case 0x65: loadReg(z80, &z80->hl.hi, z80->hl.lo); break;
		case 0x67: loadReg(z80, &z80->hl.hi, z80->af.hi); break;
		case 0x68: loadReg(z80, &z80->hl.lo, z80->bc.hi); break;
		case 0x69: loadReg(z80, &z80->hl.lo, z80->bc.lo); break;
		case 0x6A: loadReg(z80, &z80->hl.lo, z80->de.hi); break;
		case 0x6B: loadReg(z80, &z80->hl.lo, z80->de.lo); break;
		case 0x6C: loadReg(z80, &z80->hl.lo, z80->hl.hi); break;
		case 0x6D: loadReg(z80, &z80->hl.lo, z80->hl.lo); break;
		case 0x6F: loadReg(z80, &z80->hl.lo, z80->af.hi); break;

		case 0x78: loadAReg(z80, z80->bc.hi); break;
		case 0x79: loadAReg(z80, z80->bc.lo); break;
		case 0x7A: loadAReg(z80, z80->de.hi); break;
		case 0x7B: loadAReg(z80, z80->de.lo); break;
		case 0x7C: loadAReg(z80, z80->hl.hi); break;
		case 0x7D: loadAReg(z80, z80->hl.lo); break;

		case 0x46: loadRegHl(z80, &z80->bc.hi); break;
		case 0x4E: loadRegHl(z80, &z80->bc.lo); break;
		case 0x56: loadRegHl(z80, &z80->de.hi); break;
		case 0x5E: loadRegHl(z80, &z80->de.lo); break;
		case 0x66: loadRegHl(z80, &z80->hl.hi); break;
		case 0x6E: loadRegHl(z80, &z80->hl.lo); break;
		case 0x7E: loadRegHl(z80, &z80->af.hi); break;

		case 0x70: loadHlReg(z80, z80->bc.hi); break;
		case 0x71: loadHlReg(z80, z80->bc.lo); break;
		case 0x72: loadHlReg(z80, z80->de.hi); break;
		case 0x73: loadHlReg(z80, z80->de.lo); break;
		case 0x74: loadHlReg(z80, z80->hl.hi); break;
		case 0x75: loadHlReg(z80, z80->hl.lo); break;
		case 0x77: loadHlReg(z80, z80->af.hi); break;

		//Arithmetic
		case 0x19: addReg16(z80, &z80->hl, &z80->bc); break;
		case 0x09: addReg16(z80, &z80->hl, &z80->bc); break;
		case 0x29: addReg16(z80, &z80->hl, &z80->hl); break;
		case 0x39: addReg16(z80, &z80->hl, &z80->sp); break;

		case 0x03: incReg16(z80, &z80->bc); break;
		case 0x13: incReg16(z80, &z80->de); break;
		case 0x23: incReg16(z80, &z80->hl); break;
		case 0x33: incReg16(z80, &z80->sp); break;
		case 0x0B: decReg16(z80, &z80->bc); break;
		case 0x1B: decReg16(z80, &z80->de); break;
		case 0x2B: decReg16(z80, &z80->hl); break;
		case 0x3B: decReg16(z80, &z80->sp); break;
		case 0x05: decReg8(z80, &z80->bc.hi); break;
		case 0x15: decReg8(z80, &z80->de.hi); break;
		case 0x25: decReg8(z80, &z80->hl.hi); break;
		case 0x0D: decReg8(z80, &z80->bc.lo); break;
		case 0x1D: decReg8(z80, &z80->de.lo); break;
		case 0x3D: decReg8(z80, &z80->af.hi); break;
		case 0x35: decMemHl(z80); break;

		case 0x04: incReg8(z80, &z80->bc.hi); break;
		case 0x0C: incReg8(z80, &z80->bc.lo); break;
		case 0x14: incReg8(z80, &z80->de.hi); break;
		case 0x1C: incReg8(z80, &z80->de.lo); break;
		case 0x24: incReg8(z80, &z80->hl.hi); break;
		case 0x2C: incReg8(z80, &z80->hl.lo); break;
		case 0x3C: incReg8(z80, &z80->af.hi); break;
		case 0x34: incMemHl(z80); break;

		//8 bit reg add
		case 0x80: addReg8(z80, &z80->af.hi, z80->bc.hi); break;
		case 0x81: addReg8(z80, &z80->af.hi, z80->bc.lo); break;
		case 0x82: addReg8(z80, &z80->af.hi, z80->de.hi); break;
		case 0x83: addReg8(z80, &z80->af.hi, z80->de.lo); break;
		case 0x84: addReg8(z80, &z80->af.hi, z80->hl.hi); break;
		case 0x85: addReg8(z80, &z80->af.hi, z80->hl.lo); break;
		case 0x86: addMemHl(z80, &z80->af.hi); break;
		case 0x87: addReg8(z80, &z80->af.hi, z80->af.hi); break;
		case 0xC6: addReg8(z80, &z80->af.hi, z80FetchU8(z80)); z80->cycles += 3; break;

		//8 bit reg sub
		case 0x90: subReg8(z80, &z80->af.hi, z80->bc.hi); break;
		case 0x91: subReg8(z80, &z80->af.hi, z80->bc.lo); break;
		case 0x92: subReg8(z80, &z80->af.hi, z80->de.hi); break;
		case 0x93: subReg8(z80, &z80->af.hi, z80->de.lo); break;
		case 0x94: subReg8(z80, &z80->af.hi, z80->hl.hi); break;
		case 0x95: subReg8(z80, &z80->af.hi, z80->hl.lo); break;
		case 0x96: subMemHl(z80, &z80->af.hi); break;
		case 0xD6: subReg8(z80, &z80->af.hi, z80FetchU8(z80)); z80->cycles += 3; break;

		case 0x98: sbcReg8(z80, &z80->af.hi, z80->bc.hi); break;
		case 0x99: sbcReg8(z80, &z80->af.hi, z80->bc.lo); break;
		case 0x9A: sbcReg8(z80, &z80->af.hi, z80->de.hi); break;
		case 0x9B: sbcReg8(z80, &z80->af.hi, z80->de.lo); break;
		case 0x9C: sbcReg8(z80, &z80->af.hi, z80->hl.hi); break;
		case 0x9D: sbcReg8(z80, &z80->af.hi, z80->hl.lo); break;
		case 0x9E: sbcMemHl(z80, &z80->af.hi); break;
		case 0x9F: sbcReg8(z80, &z80->af.hi, z80->af.hi); break;

		//Jumps/Branches/Rets
		case 0x10: djnz(z80); break;
		case 0x18: jrImm(z80); break;
		case 0x20: jrImmCond(z80, getFlag(z80, FLAG_Z) == 0); break;
		case 0x28: jrImmCond(z80, getFlag(z80, FLAG_Z)); break;
		case 0x30: jrImmCond(z80, getFlag(z80, FLAG_C) == 0); break;
		case 0x38: jrImmCond(z80, getFlag(z80, FLAG_C)); break;

		//Logical
		case 0xA0: and(z80, z80->bc.hi); break;
		case 0xA1: and(z80, z80->bc.lo); break;
		case 0xA2: and(z80, z80->de.hi); break;
		case 0xA3: and(z80, z80->de.lo); break;
		case 0xA4: and(z80, z80->hl.hi); break;
		case 0xA5: and(z80, z80->hl.lo); break;
		case 0xA6: andMemHl(z80); break;
		case 0xA7: and(z80, z80->af.hi); break;
		case 0xE6: and(z80, z80FetchU8(z80)); z80->cycles += 3; break;


		case 0xA8: xor(z80, z80->bc.hi); break;
		case 0xA9: xor(z80, z80->bc.lo); break;
		case 0xAA: xor(z80, z80->de.hi); break;
		case 0xAB: xor(z80, z80->de.lo); break;
		case 0xAC: xor(z80, z80->hl.hi); break;
		case 0xAD: xor(z80, z80->hl.lo); break;
		case 0xAE: xorMemHl(z80); break;
		case 0xAF: xor(z80, z80->af.hi); break;
		case 0xEE: xor(z80, z80FetchU8(z80)); z80->cycles += 3; break;

		case 0xB0: or(z80, z80->bc.hi); break;
		case 0xB1: or(z80, z80->bc.lo); break;
		case 0xB2: or(z80, z80->de.hi); break;
		case 0xB3: or(z80, z80->de.lo); break;
		case 0xB4: or(z80, z80->hl.hi); break;
		case 0xB5: or(z80, z80->hl.lo); break;
		case 0xB6: orMemHl(z80); break;
		case 0xB7: or(z80, z80->af.hi); break;
		case 0xF6: or(z80, z80FetchU8(z80)); z80->cycles += 3; break;

		case 0xB8: cp(z80, z80->bc.hi); break;
		case 0xB9: cp(z80, z80->bc.lo); break;
		case 0xBA: cp(z80, z80->de.hi); break;
		case 0xBB: cp(z80, z80->de.lo); break;
		case 0xBC: cp(z80, z80->hl.hi); break;
		case 0xBD: cp(z80, z80->hl.lo); break;
		case 0xBE: cpMemHl(z80); break;
		case 0xBF: cp(z80, z80->af.hi); break;

		case 0xCC: callCond(z80, getFlag(z80, FLAG_Z)); break;
		case 0xCD: call(z80); break;
		case 0xC4: callCond(z80, getFlag(z80, FLAG_Z) == 0); break;
		case 0xD4: callCond(z80, getFlag(z80, FLAG_C) == 0); break;
		case 0xDC: callCond(z80, getFlag(z80, FLAG_C)); break;
		case 0xE4: callCond(z80, getFlag(z80, FLAG_PV) == 0); break;
		case 0xEC: callCond(z80, getFlag(z80, FLAG_PV)); break;
		case 0xF4: callCond(z80, getFlag(z80, FLAG_S) == 0); break;
		case 0xFC: callCond(z80, getFlag(z80, FLAG_S));  break;

		case 0xC0: retCond(z80, getFlag(z80, FLAG_Z) == 0); break;
		case 0xC8: retCond(z80, getFlag(z80, FLAG_Z)); break;
		case 0xC9: ret(z80); break;
		case 0xD0: retCond(z80, getFlag(z80, FLAG_C) == 0); break;
		case 0xD8: retCond(z80, getFlag(z80, FLAG_C)); break;
		case 0xE0: retCond(z80, getFlag(z80, FLAG_PV) == 0); break;
		case 0xE8: retCond(z80, getFlag(z80, FLAG_PV)); break;
		case 0xF0: retCond(z80, getFlag(z80, FLAG_S) == 0); break;
		case 0xF8: retCond(z80, getFlag(z80, FLAG_S)); break;

		//Jumps
		case 0xC2: jpCond(z80, getFlag(z80, FLAG_Z) == 0); break;
		case 0xC3: jp(z80); break;
		case 0xCA: jpCond(z80, getFlag(z80, FLAG_Z)); break;
		case 0xD2: jpCond(z80, getFlag(z80, FLAG_C) == 0); break;
		case 0xDA: jpCond(z80, getFlag(z80, FLAG_C)); break;
		case 0xE2: jpCond(z80, getFlag(z80, FLAG_PV) == 0); break;
		case 0xE9: jpMemHl(z80); break;
		case 0xEA: jpCond(z80, getFlag(z80, FLAG_PV)); break;
		case 0xF2: jpCond(z80, getFlag(z80, FLAG_S) == 0); break;
		case 0xFA: jpCond(z80, getFlag(z80, FLAG_S)); break;

		//Restarts
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
			printf("\n--Unimplemented Main Instruction--: 0x%02X\n", opcode);
			printf("PC: 0x%04X\n", z80->pc);
			assert(0);
			break;
	}
}

void executeBitInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		//rrc
		case 0x08: rrc(z80, &z80->bc.hi); break;
		case 0x09: rrc(z80, &z80->bc.lo); break;
		case 0x0A: rrc(z80, &z80->de.hi); break;
		case 0x0B: rrc(z80, &z80->de.lo); break;
		case 0x0C: rrc(z80, &z80->hl.hi); break;
		case 0x0D: rrc(z80, &z80->hl.lo); break;
		case 0x0E: rrcMemHl(z80); break;

		//srl
		case 0x38: srl(z80, &z80->bc.hi); break;
		case 0x39: srl(z80, &z80->bc.lo); break;
		case 0x3A: srl(z80, &z80->de.hi); break;
		case 0x3B: srl(z80, &z80->de.lo); break;
		case 0x3C: srl(z80, &z80->hl.hi); break;
		case 0x3D: srl(z80, &z80->hl.lo); break;
		case 0x3E: srlMemHl(z80); break;
		case 0x3F: srl(z80, &z80->af.hi); break;

		//Bit
		case 0x40: bit(z80, z80->bc.hi, 0); break;
		case 0x41: bit(z80, z80->bc.lo, 0); break;
		case 0x42: bit(z80, z80->de.hi, 0); break;
		case 0x43: bit(z80, z80->de.lo, 0); break;
		case 0x44: bit(z80, z80->hl.hi, 0); break;
		case 0x45: bit(z80, z80->hl.lo, 0); break;
		case 0x46: bitMemHl(z80, 0); break;
		case 0x47: bit(z80, z80->af.hi, 0); break;
		
		case 0x48: bit(z80, z80->bc.hi, 1); break;
		case 0x49: bit(z80, z80->bc.lo, 1); break;
		case 0x4A: bit(z80, z80->de.hi, 1); break;
		case 0x4B: bit(z80, z80->de.lo, 1); break;
		case 0x4C: bit(z80, z80->hl.hi, 1); break;
		case 0x4D: bit(z80, z80->hl.lo, 1); break;
		case 0x4E: bitMemHl(z80, 1); break;
		case 0x4F: bit(z80, z80->af.hi, 1); break;

		case 0x50: bit(z80, z80->bc.hi, 2); break;
		case 0x51: bit(z80, z80->bc.lo, 2); break;
		case 0x52: bit(z80, z80->de.hi, 2); break;
		case 0x53: bit(z80, z80->de.lo, 2); break;
		case 0x54: bit(z80, z80->hl.hi, 2); break;
		case 0x55: bit(z80, z80->hl.lo, 2); break;
		case 0x56: bitMemHl(z80, 2); break;
		case 0x57: bit(z80, z80->af.hi, 2); break;

		case 0x58: bit(z80, z80->bc.hi, 3); break;
		case 0x59: bit(z80, z80->bc.lo, 3); break;
		case 0x5A: bit(z80, z80->de.hi, 3); break;
		case 0x5B: bit(z80, z80->de.lo, 3); break;
		case 0x5C: bit(z80, z80->hl.hi, 3); break;
		case 0x5D: bit(z80, z80->hl.lo, 3); break;
		case 0x5E: bitMemHl(z80, 3); break;
		case 0x5F: bit(z80, z80->af.hi, 3); break;

		case 0x60: bit(z80, z80->bc.hi, 4); break;
		case 0x61: bit(z80, z80->bc.lo, 4); break;
		case 0x62: bit(z80, z80->de.hi, 4); break;
		case 0x63: bit(z80, z80->de.lo, 4); break;
		case 0x64: bit(z80, z80->hl.hi, 4); break;
		case 0x65: bit(z80, z80->hl.lo, 4); break;
		case 0x66: bitMemHl(z80, 4); break;
		case 0x67: bit(z80, z80->af.hi, 4); break;

		case 0x68: bit(z80, z80->bc.hi, 5); break;
		case 0x69: bit(z80, z80->bc.lo, 5); break;
		case 0x6A: bit(z80, z80->de.hi, 5); break;
		case 0x6B: bit(z80, z80->de.lo, 5); break;
		case 0x6C: bit(z80, z80->hl.hi, 5); break;
		case 0x6D: bit(z80, z80->hl.lo, 5); break;
		case 0x6E: bitMemHl(z80, 5); break;
		case 0x6F: bit(z80, z80->af.hi, 5); break;

		case 0x70: bit(z80, z80->bc.hi, 6); break;
		case 0x71: bit(z80, z80->bc.lo, 6); break;
		case 0x72: bit(z80, z80->de.hi, 6); break;
		case 0x73: bit(z80, z80->de.lo, 6); break;
		case 0x74: bit(z80, z80->hl.hi, 6); break;
		case 0x75: bit(z80, z80->hl.lo, 6); break;
		case 0x76: bitMemHl(z80, 6); break;
		case 0x77: bit(z80, z80->af.hi, 6); break;

		case 0x78: bit(z80, z80->bc.hi, 7); break;
		case 0x79: bit(z80, z80->bc.lo, 7); break;
		case 0x7A: bit(z80, z80->de.hi, 7); break;
		case 0x7B: bit(z80, z80->de.lo, 7); break;
		case 0x7C: bit(z80, z80->hl.hi, 7); break;
		case 0x7D: bit(z80, z80->hl.lo, 7); break;
		case 0x7E: bitMemHl(z80, 7); break;
		case 0x7F: bit(z80, z80->af.hi, 7); break;

		case 0x80: res(z80, &z80->bc.hi, 0); break;
		case 0x81: res(z80, &z80->bc.lo, 0); break;
		case 0x82: res(z80, &z80->de.hi, 0); break;
		case 0x83: res(z80, &z80->de.lo, 0); break;
		case 0x84: res(z80, &z80->hl.hi, 0); break;
		case 0x85: res(z80, &z80->hl.lo, 0); break;
		case 0x86: resMemHl(z80, 0); break;
		case 0x87: res(z80, &z80->af.hi, 0); break;

		case 0x88: res(z80, &z80->bc.hi, 1); break;
		case 0x89: res(z80, &z80->bc.lo, 1); break;
		case 0x8A: res(z80, &z80->de.hi, 1); break;
		case 0x8B: res(z80, &z80->de.lo, 1); break;
		case 0x8C: res(z80, &z80->hl.hi, 1); break;
		case 0x8D: res(z80, &z80->hl.lo, 1); break;
		case 0x8E: resMemHl(z80, 1); break;
		case 0x8F: res(z80, &z80->af.hi, 1); break;

		case 0x90: res(z80, &z80->bc.hi, 2); break;
		case 0x91: res(z80, &z80->bc.lo, 2); break;
		case 0x92: res(z80, &z80->de.hi, 2); break;
		case 0x93: res(z80, &z80->de.lo, 2); break;
		case 0x94: res(z80, &z80->hl.hi, 2); break;
		case 0x95: res(z80, &z80->hl.lo, 2); break;
		case 0x96: resMemHl(z80, 2); break;
		case 0x97: res(z80, &z80->af.hi, 2); break;

		case 0x98: res(z80, &z80->bc.hi, 3); break;
		case 0x99: res(z80, &z80->bc.lo, 3); break;
		case 0x9A: res(z80, &z80->de.hi, 3); break;
		case 0x9B: res(z80, &z80->de.lo, 3); break;
		case 0x9C: res(z80, &z80->hl.hi, 3); break;
		case 0x9D: res(z80, &z80->hl.lo, 3); break;
		case 0x9E: resMemHl(z80, 3); break;
		case 0x9F: res(z80, &z80->af.hi, 3); break;

		case 0xA0: res(z80, &z80->bc.hi, 4); break;
		case 0xA1: res(z80, &z80->bc.lo, 4); break;
		case 0xA2: res(z80, &z80->de.hi, 4); break;
		case 0xA3: res(z80, &z80->de.lo, 4); break;
		case 0xA4: res(z80, &z80->hl.hi, 4); break;
		case 0xA5: res(z80, &z80->hl.lo, 4); break;
		case 0xA6: resMemHl(z80, 4); break;
		case 0xA7: res(z80, &z80->af.hi, 4); break;

		case 0xA8: res(z80, &z80->bc.hi, 5); break;
		case 0xA9: res(z80, &z80->bc.lo, 5); break;
		case 0xAA: res(z80, &z80->de.hi, 5); break;
		case 0xAB: res(z80, &z80->de.lo, 5); break;
		case 0xAC: res(z80, &z80->hl.hi, 5); break;
		case 0xAD: res(z80, &z80->hl.lo, 5); break;
		case 0xAE: resMemHl(z80, 5); break;
		case 0xAF: res(z80, &z80->af.hi, 5); break;

		case 0xB0: res(z80, &z80->bc.hi, 6); break;
		case 0xB1: res(z80, &z80->bc.lo, 6); break;
		case 0xB2: res(z80, &z80->de.hi, 6); break;
		case 0xB3: res(z80, &z80->de.lo, 6); break;
		case 0xB4: res(z80, &z80->hl.hi, 6); break;
		case 0xB5: res(z80, &z80->hl.lo, 6); break;
		case 0xB6: resMemHl(z80, 6); break;
		case 0xB7: res(z80, &z80->af.hi, 6); break;

		case 0xB8: res(z80, &z80->bc.hi, 7); break;
		case 0xB9: res(z80, &z80->bc.lo, 7); break;
		case 0xBA: res(z80, &z80->de.hi, 7); break;
		case 0xBB: res(z80, &z80->de.lo, 7); break;
		case 0xBC: res(z80, &z80->hl.hi, 7); break;
		case 0xBD: res(z80, &z80->hl.lo, 7); break;
		case 0xBE: resMemHl(z80, 7); break;
		case 0xBF: res(z80, &z80->af.hi, 7); break;

		case 0xC7: set(z80, &z80->af.hi, 0); break;

		default:
			printf("\n--Unimplemented Bit Instruction--: 0x%02X\n", opcode);
			printf("PC: 0x%04X\n", z80->pc);
			assert(0);
			break;
	}
}

void executeIxInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		//Arithmetic
		case 0x09: addReg16(z80, &z80->ix, &z80->bc); break;
		case 0x19: addReg16(z80, &z80->ix, &z80->de); break;
		case 0x29: addReg16(z80, &z80->ix, &z80->ix); break;
		case 0x39: addReg16(z80, &z80->ix, &z80->sp); break;

		case 0x23: incReg16(z80, &z80->ix); z80->cycles += 4; break;
		case 0x34: incMemIx(z80); break;
		case 0x35: decMemIx(z80); break;

		//Load value from ix + offset into reg8
		case 0x46: loadRegIx(z80, &z80->bc.hi); break;
		case 0x4E: loadRegIx(z80, &z80->bc.lo); break;
		case 0x56: loadRegIx(z80, &z80->de.hi); break;
		case 0x5E: loadRegIx(z80, &z80->de.lo); break;
		case 0x66: loadRegIx(z80, &z80->hl.hi); break;
		case 0x6E: loadRegIx(z80, &z80->hl.lo); break;
		case 0x7E: loadRegIx(z80, &z80->af.hi); break;

		//Load reg8 into memory location ix + immediate s8
		case 0x36: loadIxImm(z80); break;
		case 0x70: loadIxReg(z80, z80->bc.hi); break;
		case 0x71: loadIxReg(z80, z80->bc.lo); break;
		case 0x72: loadIxReg(z80, z80->de.hi); break;
		case 0x73: loadIxReg(z80, z80->de.lo); break;
		case 0x74: loadIxReg(z80, z80->hl.hi); break;
		case 0x75: loadIxReg(z80, z80->hl.lo); break;
		case 0x77: loadIxReg(z80, z80->af.hi); break;

		case 0x86: addMemIx(z80, &z80->af.hi); break;

		case 0x21: loadReg16(z80, &z80->ix); z80->cycles += 4; break;

		//Logical
		case 0xB6: orMemIx(z80); break;

		case 0xE9: jpMemIx(z80); break;

		case 0xE1: pop(z80, &z80->ix); break;
		case 0xE5: push(z80, &z80->ix); break;
		default:
			printf("\n--Unimplemented Ix Instruction--: 0x%02X\n", opcode);
			printf("PC: 0x%04X\n", z80->pc);
			assert(0);
			break;
	}
}

void executeIxBitInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		case 0x56: bitIx(z80, 2); break;
		case 0x5E: bitIx(z80, 3); break;
		case 0x6E: bitIx(z80, 5); break;
		case 0x76: bitIx(z80, 6); break;
		case 0x7E: bitIx(z80, 7); break;
		default:
			printf("\n--Unimplemented Ix Bit Instruction--: 0x%02X\n", opcode);
			printf("PC: 0x%04X\n", z80->pc);
			assert(0);
			break;
	}
}

void executeExtendedInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		//Loads
		case 0x43: loadMemReg16(z80, &z80->bc); break;
		case 0x53: loadMemReg16(z80, &z80->de); break;
		case 0x73: loadMemReg16(z80, &z80->sp); break;
		case 0x5F: loadAWithR(z80); break;
		case 0x7B: load16Reg(z80, &z80->sp); z80->cycles += 4; break;

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

		//Arithmetic
		case 0x42: sbcReg16(z80, &z80->hl, &z80->bc); break;
		case 0x52: sbcReg16(z80, &z80->hl, &z80->de); break;
		case 0x62: sbcReg16(z80, &z80->hl, &z80->hl); break;
		case 0x72: sbcReg16(z80, &z80->hl, &z80->sp); break;

		case 0x4A: adcReg16(z80, &z80->hl, &z80->bc); break;
		case 0x5A: adcReg16(z80, &z80->hl, &z80->de); break;
		case 0x6A: adcReg16(z80, &z80->hl, &z80->hl); break;
		case 0x7A: adcReg16(z80, &z80->hl, &z80->sp); break;

		//Misc
		case 0x44: neg(z80); break;

		//Returns
		case 0x45: retn(z80); break;
		case 0x4D: reti(z80); break;
		case 0x55: retn(z80); break;
		case 0x5D: retn(z80); break;
		case 0x65: retn(z80); break;
		case 0x6D: retn(z80); break;
		case 0x75: retn(z80); break;
		case 0x7D: retn(z80); break;

		//I/O instructions
		case 0xA0: ldi(z80); break;
		case 0xA3: outi(z80); break;
		case 0xB0: ldir(z80); break;
		case 0xB3: otir(z80); break;

		default:
			printf("\n--Unimplemented Extended Instruction--: 0x%02X\n", opcode);
			printf("PC: 0x%04X\n", z80->pc);
			assert(0);
			break;
	}
}

void executeIyInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		case 0x21: loadReg16(z80, &z80->iy); z80->cycles += 4; break;
		case 0x23: incReg16(z80, &z80->iy); z80->cycles += 4; break;

		case 0x7E: loadRegIy(z80, &z80->af.hi); break;

		case 0xE9: jpMemIy(z80); break;

		case 0xE1: pop(z80, &z80->iy); break;
		case 0xE5: push(z80, &z80->iy); break;
		default:
			printf("\n--Unimplemented Iy Instruction--: 0x%02X\n", opcode);
			printf("PC: 0x%04X\n", z80->pc);
			assert(0);
			break;
	}
}

void executeIyBitInstruction(struct Z80* z80, u8 opcode)
{
	switch (opcode) {
		default:
			printf("\n--Unimplemented Iy Bit Instruction--: 0x%02X\n", opcode);
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
	z80WriteU8(z80, value, z80->hl.value);
	z80->cycles = 10;
}

void loadAReg(struct Z80* z80, u8 reg)
{
	z80->af.hi = reg;
	z80->cycles = 4;
}

void loadRegHl(struct Z80* z80, u8* reg)
{
	*reg = z80ReadU8(z80, z80->hl.value);
	z80->cycles = 7;
}

void loadMemReg16(struct Z80* z80, union Register* reg)
{
	u16 address = z80FetchU16(z80);
	z80WriteU8(z80, reg->lo, address);
	z80WriteU8(z80, reg->hi, address + 1);

	z80->cycles = 16;
}

void loadMemReg8(struct Z80* z80, u8 reg)
{
	u16 address = z80FetchU16(z80);
	z80WriteU8(z80, reg, address);

	z80->cycles = 13;
}

void loadHlReg(struct Z80* z80, u8 reg)
{
	z80WriteU8(z80, reg, z80->hl.value);
	z80->cycles = 7;
}

void load16Reg(struct Z80* z80, union Register* reg)
{
	u16 address = z80FetchU16(z80);
	
	u8 lo = z80ReadU8(z80, address);
	u8 hi = z80ReadU8(z80, address + 1);
	u16 value = ((hi << 8) | lo);

	reg->value = value;
	z80->cycles = 16;
}

void load16A(struct Z80* z80)
{
	u16 address = z80FetchU16(z80);
	u8 value = z80ReadU8(z80, address);

	z80->af.hi = value;
	z80->cycles = 13;
}

void loadReg(struct Z80* z80, u8* destReg, u8 sourceReg)
{
	*destReg = sourceReg;
	z80->cycles = 4;
}

void loadRegMem(struct Z80* z80, u8* destReg, union Register* reg)
{
	u16 address = reg->value;
	u8 value = z80ReadU8(z80, address);

	*destReg = value;

	z80->cycles = 7;
}

void loadReg8Mem(struct Z80* z80, union Register mem, u8 reg)
{
	z80WriteU8(z80, reg, mem.value);
	z80->cycles = 7;
}

void loadAWithR(struct Z80* z80)
{
	z80->af.hi = z80->ir.lo;

	z80ClearFlag(z80, (FLAG_N | FLAG_H));
	z80AffectFlag(z80, z80IsSigned8(z80->ir.lo), FLAG_S);
	z80AffectFlag(z80, z80->ir.lo == 0, FLAG_Z);
	z80AffectFlag(z80, z80->iff2, FLAG_PV);

	z80->cycles = 9;
}

void loadSpReg(struct Z80* z80, union Register* reg)
{
	z80->sp = reg->value;
	z80->cycles = 6;
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
	u8 result = (*reg) - 1;

	z80SetFlag(z80, FLAG_N);
	z80AffectFlag(z80, z80IsSigned8(result), FLAG_S);
	z80AffectFlag(z80, result == 0, FLAG_Z);
	z80AffectFlag(z80, z80HalfBorrowOccured8(*reg, 1), FLAG_H);
	z80AffectFlag(z80, z80OverflowFromSub8(*reg, 1), FLAG_PV);

	(*reg)--;
	z80->cycles = 4;
}

void decMemHl(struct Z80* z80)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	decReg8(z80, &value);

	z80WriteU8(z80, value, z80->hl.value);

	z80->cycles += 7;
}

void incReg8(struct Z80* z80, u8* reg)
{
	u8 result = (*reg) + 1;

	z80ClearFlag(z80, FLAG_N);
	z80AffectFlag(z80, z80IsSigned8(result), FLAG_S);
	z80AffectFlag(z80, result == 0, FLAG_Z);
	z80AffectFlag(z80, z80HalfCarryOccured8(*reg, 1), FLAG_H);
	z80AffectFlag(z80, z80OverflowFromAdd8(*reg, 1), FLAG_PV);

	(*reg)++;
	z80->cycles = 4;
}

void incMemHl(struct Z80* z80)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	incReg8(z80, &value);

	z80WriteU8(z80, value, z80->hl.value);

	z80->cycles += 7;
}

void addReg16(struct Z80* z80, union Register* destReg, union Register *sourceReg)
{
	z80ClearFlag(z80, FLAG_N);
	z80AffectFlag(z80, z80HalfCarryOccured16(destReg->value, sourceReg->value), FLAG_H);
	z80AffectFlag(z80, z80CarryOccured16(destReg->value, sourceReg->value), FLAG_C);

	destReg->value += sourceReg->value;

	z80->cycles = 11;
	if (destReg == &z80->ix || destReg == &z80->iy);
		z80->cycles += 4;
}

void addReg8(struct Z80* z80, u8* destReg, u8 sourceReg)
{
	u8 dest_reg = (*destReg);
	u8 result = dest_reg + sourceReg;

	z80ClearFlag(z80, FLAG_N);
	z80AffectFlag(z80, z80IsSigned8(result), FLAG_S);
	z80AffectFlag(z80, result == 0, FLAG_Z);
	z80AffectFlag(z80, z80HalfCarryOccured8(dest_reg, sourceReg), FLAG_H);
	z80AffectFlag(z80, z80OverflowFromAdd8(dest_reg, sourceReg), FLAG_PV);
	z80AffectFlag(z80, z80CarryOccured8(dest_reg, sourceReg), FLAG_C);

	(*destReg) += sourceReg;

	z80->cycles = 4;
}

void addMemHl(struct Z80* z80, u8* destReg)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	addReg8(z80, destReg, value);

	z80->cycles += 3;
}

void adcReg16(struct Z80* z80, union Register* destReg, union Register* sourceReg)
{
	u8 carry = getFlag(z80, FLAG_C);
	u16 result = destReg->value + sourceReg->value + carry;

	z80ClearFlag(z80, FLAG_N);
	z80AffectFlag(z80, z80IsSigned16(result), FLAG_S);
	z80AffectFlag(z80, result == 0, FLAG_Z);
	z80AffectFlag(z80, z80HalfCarryOccured16(destReg->value, sourceReg->value + carry), FLAG_H);
	z80AffectFlag(z80, z80OverflowFromAdd16(destReg->value, sourceReg->value + carry), FLAG_PV);
	z80AffectFlag(z80, z80CarryOccured16(destReg->value, sourceReg->value + carry), FLAG_C);

	destReg->value = result;
	z80->cycles = 15;
}

void subReg8(struct Z80* z80, u8* destReg, u8 sourceReg)
{
	u8 dest_reg = (*destReg);
	u8 result = dest_reg - sourceReg;

	z80SetFlag(z80, FLAG_N);
	z80AffectFlag(z80, z80IsSigned8(result), FLAG_S);
	z80AffectFlag(z80, result == 0, FLAG_Z);
	z80AffectFlag(z80, z80HalfBorrowOccured8(dest_reg, sourceReg), FLAG_H);
	z80AffectFlag(z80, z80OverflowFromSub8(dest_reg, sourceReg), FLAG_PV);
	z80AffectFlag(z80, z80BorrowOccured8(dest_reg, sourceReg), FLAG_C);

	(*destReg) -= sourceReg;

	z80->cycles = 4;
}

void subMemHl(struct Z80* z80, u8* destReg)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	subReg8(z80, destReg, value);

	z80->cycles += 3;
}

void sbcReg8(struct Z80* z80, u8* destReg, u8 sourceReg)
{
	u8 carry = getFlag(z80, FLAG_C);
	u8 dest_reg = (*destReg);
	u8 result = dest_reg - sourceReg - carry;

	z80SetFlag(z80, FLAG_N);
	z80AffectFlag(z80, z80IsSigned8(result), FLAG_S);
	z80AffectFlag(z80, result == 0, FLAG_Z);
	z80AffectFlag(z80, z80HalfBorrowOccured8(dest_reg, sourceReg - carry), FLAG_H);
	z80AffectFlag(z80, z80OverflowFromSub8(dest_reg, sourceReg - carry), FLAG_PV);
	z80AffectFlag(z80, z80BorrowOccured8(dest_reg + carry, sourceReg), FLAG_C);
	
	*destReg = result;
	z80->cycles = 4;
}

void sbcMemHl(struct Z80* z80, u8* destReg)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	sbcReg8(z80, destReg, value);

	z80->cycles += 3;
}

void sbcReg16(struct Z80* z80, union Register* destReg, union Register *sourceReg)
{
	u8 carry = getFlag(z80, FLAG_C);
	u16 result = destReg->value - sourceReg->value - carry;

	z80SetFlag(z80, FLAG_N);
	z80AffectFlag(z80, z80IsSigned16(result), FLAG_S);
	z80AffectFlag(z80, result == 0, FLAG_Z);
	z80AffectFlag(z80, z80HalfBorrowOccured16(destReg->value, sourceReg->value - carry), FLAG_H);
	z80AffectFlag(z80, z80OverflowFromSub16(destReg->value, sourceReg->value - carry), FLAG_PV);
	z80AffectFlag(z80, z80BorrowOccured16(destReg->value + carry, sourceReg->value), FLAG_C);

	destReg->value = result;
	z80->cycles = 15;
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
	z80WriteU8(z80, (z80->pc >> 8) & 0xFF, z80->sp);
	z80->sp--;
	z80WriteU8(z80, (z80->pc & 0xFF), z80->sp);

	z80->pc = vector;
	z80->cycles = 11;
}

void call(struct Z80* z80)
{
	u16 address = z80FetchU16(z80);
	z80->sp--;
	z80WriteU8(z80, (z80->pc >> 8) & 0xFF, z80->sp);
	z80->sp--;
	z80WriteU8(z80, (z80->pc & 0xFF), z80->sp);

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
	u8 lo = z80ReadU8(z80, z80->sp);
	z80->sp++;
	u8 hi = z80ReadU8(z80, z80->sp);
	z80->sp++;

	u16 return_address = ((hi << 8) | lo);
	z80->pc = return_address;
	z80->cycles = 10;
}

void reti(struct Z80* z80)
{
	ret(z80);
	z80->iff1 = z80->iff2;
	z80->cycles += 4;
}

void retn(struct Z80* z80)
{
	ret(z80);
	z80->iff1 = z80->iff2;
	z80->cycles += 4;
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
	else {
		z80->pc += 2;
		z80->cycles = 10;
	}
}

void jpMemHl(struct Z80* z80)
{
	z80->pc = z80->hl.value;
	z80->cycles = 4;
}

void xor(struct Z80* z80, u8 reg)
{
	u8 result = z80->af.hi ^ reg;
	z80->af.hi ^= reg;

	z80AffectFlag(z80, (result == 0), FLAG_Z);
	z80AffectFlag(z80, z80IsEvenParity(result), FLAG_PV);
	z80AffectFlag(z80, z80IsSigned8(result), FLAG_S);

	z80ClearFlag(z80, (FLAG_C | FLAG_N | FLAG_H));
	z80->cycles = 4;
}

void xorMemHl(struct Z80* z80)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	xor(z80, value);

	z80->cycles += 3;
}

void or(struct Z80 * z80, u8 reg)
{
	u8 result = z80->af.hi | reg;
	z80->af.hi |= reg;

	z80AffectFlag(z80, (result == 0), FLAG_Z);
	z80AffectFlag(z80, z80IsEvenParity(result), FLAG_PV);
	z80AffectFlag(z80, z80IsSigned8(result), FLAG_S);

	z80ClearFlag(z80, (FLAG_C | FLAG_N | FLAG_H));
	z80->cycles = 4;
}

void orMemHl(struct Z80* z80)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	or(z80, value);

	z80->cycles += 3;
}

void and(struct Z80* z80, u8 reg)
{
	u8 result = z80->af.hi & reg;
	z80->af.hi &= reg;

	z80AffectFlag(z80, (result == 0), FLAG_Z);
	z80AffectFlag(z80, z80IsEvenParity(result), FLAG_PV);
	z80AffectFlag(z80, z80IsSigned8(result), FLAG_S);

	z80SetFlag(z80, FLAG_H);
	z80ClearFlag(z80, (FLAG_C | FLAG_N));
	z80->cycles = 4;
}

void andMemHl(struct Z80* z80)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
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

void rra(struct Z80* z80)
{
	u8 carry = getFlag(z80, FLAG_C);
	u8 lsb = z80->af.hi & 0x1;

	z80ClearFlag(z80, (FLAG_N | FLAG_H));

	z80->af.hi >>= 1;
	z80->af.hi |= (carry << 7);

	if (lsb) {
		z80SetFlag(z80, FLAG_C);
	}
	else {
		z80ClearFlag(z80, FLAG_C);
	}

	z80->cycles = 4;
}

void rla(struct Z80* z80)
{
	u8 carry = getFlag(z80, FLAG_C);
	u8 msb = (z80->af.hi >> 7) & 0x1;
	z80ClearFlag(z80, (FLAG_N | FLAG_H));

	z80->af.hi <<= 1;
	z80->af.hi |= carry;
	if (msb) {
		z80SetFlag(z80, FLAG_C);
	}
	else {
		z80ClearFlag(z80, FLAG_C);
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
	z80WriteU8(z80, reg->hi, z80->sp);
	z80->sp--;
	z80WriteU8(z80, reg->lo, z80->sp);

	z80->cycles = 11;
	if (reg == &z80->ix || reg == &z80->iy)
		z80->cycles += 4;
}

void pop(struct Z80* z80, union Register* reg)
{
	reg->lo = z80ReadU8(z80, z80->sp);
	z80->sp++;
	reg->hi = z80ReadU8(z80, z80->sp);
	z80->sp++;

	z80->cycles = 10;
	if (reg == &z80->ix || reg == &z80->iy)
		z80->cycles += 4;
}

void outa(struct Z80* z80)
{
	u8 io_port = z80FetchU8(z80);
	ioWriteU8(z80->io, z80->af.hi, io_port);

	z80->cycles = 11;
}

void outi(struct Z80* z80)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	u8 port = z80->bc.lo;

	ioWriteU8(z80->io, value, port);

	z80->hl.value++;
	z80->bc.hi--;

	z80SetFlag(z80, FLAG_N);
	z80AffectFlag(z80, z80->bc.hi == 0, FLAG_Z);

	z80->cycles = 16;
}

void ina(struct Z80* z80)
{
	u8 io_port = z80FetchU8(z80);

	u8 open_bus = ((io_port >= 0x0) && (io_port <= 0x3F));
	if (open_bus) {
		z80->af.hi = io_port;
	}
	else {
		u8 io_value = ioReadU8(z80->io, io_port);
		z80->af.hi = io_value;
	}

	z80->cycles = 11;
}

void out(struct Z80* z80, u8 destPort, u8 sourceReg)
{
	ioWriteU8(z80->io, destPort, sourceReg);
	z80->cycles = 12;
}

void in(struct Z80* z80, u8 sourcePort, u8* destReg, u8 opcode)
{
	u8 io_value = 0x0;
	u8 open_bus = ((sourcePort >= 0x0) && (sourcePort <= 0x3F));
	if (open_bus) {
		io_value = opcode;
	}else
		io_value = ioReadU8(z80->io, sourcePort);

	if(destReg != NULL)
		*destReg = io_value;

	z80AffectFlag(z80, (io_value == 0), FLAG_Z);
	z80AffectFlag(z80, z80IsEvenParity(io_value), FLAG_PV);
	z80AffectFlag(z80, z80IsSigned8(io_value), FLAG_S);

	z80ClearFlag(z80, (FLAG_H | FLAG_N));
	z80->cycles = 12;
}

void otir(struct Z80* z80)
{
	z80->cycles = ((z80->bc.hi != 0) ? 21 : 16);
	
	if (z80->bc.hi != 0) {
		//Byte from address hl written to port c
		u8 value = z80ReadU8(z80, z80->hl.value);
		u8 io_port = z80->bc.lo;
		ioWriteU8(z80->io, value, io_port);

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
		u8 value = z80ReadU8(z80, z80->hl.value);
		z80WriteU8(z80, value, z80->de.value);

		z80->hl.value++;
		z80->de.value++;
		z80->bc.value--;

		if (z80->bc.value != 0)
			z80->pc -= 2;
	}
	z80ClearFlag(z80, (FLAG_N | FLAG_PV | FLAG_H));
}

void ldi(struct Z80* z80)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	z80WriteU8(z80, value, z80->de.value);

	z80->hl.value++;
	z80->de.value++;
	z80->bc.value--;

	z80AffectFlag(z80, z80->bc.value != 0, FLAG_PV);
	z80ClearFlag(z80, (FLAG_N | FLAG_H));

	z80->cycles = 16;
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

void cp(struct Z80* z80, u8 reg)
{
	u8 result = z80->af.hi - reg;

	z80SetFlag(z80, FLAG_N);
	z80AffectFlag(z80, result == 0, FLAG_Z);
	z80AffectFlag(z80, z80OverflowFromSub8(z80->af.hi, reg), FLAG_PV);
	z80AffectFlag(z80, z80IsSigned8(result), FLAG_S);
	z80AffectFlag(z80, z80BorrowOccured8(z80->af.hi, reg), FLAG_C);
	z80AffectFlag(z80, z80HalfBorrowOccured8(z80->af.hi, reg), FLAG_H);

	z80->cycles = 4;
}

void cpMemHl(struct Z80* z80)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	cp(z80, value);

	z80->cycles += 3;
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

void ccf(struct Z80* z80)
{
	u8 carry = getFlag(z80, FLAG_C);

	z80ClearFlag(z80, FLAG_N);
	z80AffectFlag(z80, carry, FLAG_H);
	z80AffectFlag(z80, carry ^ 1, FLAG_C);

	z80->cycles = 4;
}

void neg(struct Z80* z80)
{
	u8 result = 0 - z80->af.hi;

	z80SetFlag(z80, FLAG_N);
	z80AffectFlag(z80, z80IsSigned8(result), FLAG_S);
	z80AffectFlag(z80, result == 0, FLAG_Z);
	z80AffectFlag(z80, z80HalfBorrowOccured8(0, z80->af.hi), FLAG_H);
	z80AffectFlag(z80, z80OverflowFromSub8(0, z80->af.hi), FLAG_PV);
	z80AffectFlag(z80, z80BorrowOccured8(0, z80->af.hi), FLAG_C);

	z80->af.hi = result;
	z80->cycles = 8;
}

void scf(struct Z80* z80)
{
	z80ClearFlag(z80, (FLAG_N | FLAG_H));
	z80SetFlag(z80, FLAG_C);

	z80->cycles = 4;
}

void rrc(struct Z80* z80, u8* reg)
{
	u8 reg_value = (*reg);
	u8 lsb = (reg_value & 0x1);

	reg_value >>= 1;

	z80ClearFlag(z80, (FLAG_N | FLAG_H));
	if (lsb) {
		z80SetFlag(z80, FLAG_C);
		reg_value = setBit(reg_value, 7);
	}
	else {
		z80ClearFlag(z80, FLAG_C);
		reg_value = clearBit(reg_value, 7);
	}
	(*reg) = reg_value;

	z80AffectFlag(z80, z80IsSigned8(reg_value), FLAG_S);
	z80AffectFlag(z80, reg_value == 0, FLAG_Z);
	z80AffectFlag(z80, z80IsEvenParity(reg_value), FLAG_PV);

	z80->cycles = 8;
}

void rrcMemHl(struct Z80* z80)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	rrc(z80, &value);

	z80WriteU8(z80, value, z80->hl.value);

	z80->cycles = 15;
}

void srl(struct Z80* z80, u8* reg)
{
	u8 reg_value = (*reg);
	u8 lsb = (reg_value & 0x1);

	z80ClearFlag(z80, (FLAG_N | FLAG_H));

	reg_value >>= 1;
	reg_value = clearBit(reg_value, 7);
	if (lsb) {
		z80SetFlag(z80, FLAG_C);
	}
	else {
		z80ClearFlag(z80, FLAG_C);
	}

	*reg = reg_value;

	z80ClearFlag(z80, FLAG_S);
	z80AffectFlag(z80, reg_value == 0, FLAG_Z);
	z80AffectFlag(z80, z80IsEvenParity(reg_value), FLAG_PV);

	z80->cycles = 8;
}

void srlMemHl(struct Z80* z80)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	srl(z80, &value);

	z80WriteU8(z80, value, z80->hl.value);

	z80->cycles = 15;
}

void bit(struct Z80* z80, u8 reg, u8 bit)
{
	u8 test = testBit(reg, bit);

	z80AffectFlag(z80, test == 0, FLAG_Z);
	z80ClearFlag(z80, FLAG_N);
	z80SetFlag(z80, FLAG_H);

	z80->cycles = 8;
}

void bitMemHl(struct Z80* z80, u8 bitToTest)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	bit(z80, value, bitToTest);

	z80->cycles += 4;
}

void res(struct Z80* z80, u8* reg, u8 bit)
{
	*reg = clearBit(*reg, bit);
	z80->cycles = 8;
}

void resMemHl(struct Z80* z80, u8 bit)
{
	u8 value = z80ReadU8(z80, z80->hl.value);
	res(z80, &value, bit);

	z80WriteU8(z80, value, z80->hl.value);

	z80->cycles += 7;
}

void set(struct Z80* z80, u8* reg, u8 bit)
{
	*reg = setBit(*reg, bit);
	z80->cycles = 8;
}

void loadRegIx(struct Z80* z80, u8* reg)
{
	s8 offset = (s8)z80FetchU8(z80);
	u8 value = z80ReadU8(z80, z80->ix.value + offset);

	*reg = value;

	z80->cycles = 19;
}

void loadIxReg(struct Z80* z80, u8 reg)
{
	s8 offset = (s8)z80FetchU8(z80);
	u16 address = z80->ix.value + offset;

	z80WriteU8(z80, reg, address);

	z80->cycles = 19;
}

void loadIxImm(struct Z80* z80)
{
	//ix offset byte is first in memory
	s8 offset = (s8)z80FetchU8(z80);
	u16 address = z80->ix.value + offset;

	u8 imm_value = z80FetchU8(z80);
	z80WriteU8(z80, imm_value, address);

	z80->cycles = 19;
}

void incMemIx(struct Z80* z80)
{
	u8 offset = z80FetchU8(z80);
	u16 address = z80->ix.value + offset;

	u8 value = z80ReadU8(z80, address);
	incReg8(z80, &value);

	z80WriteU8(z80, value, address);

	z80->cycles += 19;
}

void addMemIx(struct Z80* z80, u8* reg)
{
	s8 offset = (s8)z80FetchU8(z80);
	u16 address = z80->ix.value + offset;

	u8 value = z80ReadU8(z80, address);
	addReg8(z80, reg, value);

	z80->cycles += 15;
}

void decMemIx(struct Z80* z80)
{
	u8 offset = z80FetchU8(z80);
	u16 address = z80->ix.value + offset;

	u8 value = z80ReadU8(z80, address);
	decReg8(z80, &value);

	z80WriteU8(z80, value, address);

	z80->cycles += 19;
}

void orMemIx(struct Z80* z80)
{
	s8 offset = (s8)z80FetchU8(z80);
	u16 address = z80->ix.value + offset;

	u8 value = z80ReadU8(z80, address);
	or(z80, value);

	z80->cycles += 15;
}

void jpMemIx(struct Z80* z80)
{
	z80->pc = z80->ix.value;
	z80->cycles = 8;
}

void bitIx(struct Z80* z80, u8 bit)
{
	u8 offset = z80FetchU8(z80);
	u8 value = z80ReadU8(z80, z80->ix.value + offset);

	z80AffectFlag(z80, testBit(value, bit) == 0, FLAG_Z);
	z80ClearFlag(z80, FLAG_N);
	z80SetFlag(z80, FLAG_H);

	z80->cycles = 20;
}

void loadRegIy(struct Z80* z80, u8* reg)
{
	s8 offset = (s8)z80FetchU8(z80);
	u8 value = z80ReadU8(z80, z80->iy.value + offset);

	*reg = value;

	z80->cycles = 19;
}

void jpMemIy(struct Z80* z80)
{
	z80->pc = z80->iy.value;
	z80->cycles = 8;
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
