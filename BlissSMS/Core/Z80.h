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

//Used for testing z80 core by itself
struct Cpm {
	u8 memory[0x10000];
};

void cpmLoadRom(struct Z80* z80, const char *path);
void cpmHandleSysCalls(struct Z80* z80);
void cpmWriteMem8(struct Z80* z80, u16 address, u8 value);
u8 cpmReadMem8(struct Z80* z80, u16 address);


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
	u16 mem_ptr;

	enum IntMode interrupt_mode;
	u16 cycles;
	u8 iff1;
	u8 iff2;
	u8 process_interrupt_delay; //flag used for instruction delay after ei is executed
	u8 halted;
	u8 irq_requested;

	u8 service_nmi;
	u8 last_daa_operation;
	u8 opcode;
	u8 ext_opcode;

	struct Bus* bus;
	struct Io* io;

	struct Cpm cpm;
	u8 cpm_stub_enabled;
	u8 cpm_test_finished;
};

void z80DebugOutput(struct Z80* z80);

void z80Init(struct Z80* z80);
void z80ConnectBus(struct Z80 *z80, struct Bus* bus);
void z80ConnectIo(struct Z80 *z80, struct Io* io);
void z80AffectFlag(struct Z80* z80, u8 cond, u8 flags);
void z80SetFlag(struct Z80* z80, u8 flags);
void z80ClearFlag(struct Z80* z80, u8 flags);
void z80ClearFlagCopyBits(struct Z80* z80);
u8 getFlag(struct Z80* z80, u8 flag);

void z80HandleInterrupts(struct Z80* z80, struct Vdp *vdp);
void z80RequestIrq(struct System* sys);

u8 z80OverflowFromAdd8(u8 op1, u8 op2, u8 carry);
u8 z80OverflowFromSub8(u8 op1, u8 op2, u8 carry);
u8 z80OverflowFromAdd16(u16 op1, u16 op2, u8 carry);
u8 z80OverflowFromSub16(u16 op1, u16 op2, u8 carry);
u8 z80IsEvenParity(u8 value);
u8 z80IsSigned8(u8 value);
u8 z80IsSigned16(u16 value);

//8 bit carry/borrow check
u8 z80CarryOccured8(u8 op1, u8 op2, u8 carry); 
u8 z80HalfCarryOccured8(u8 op1, u8 op2, u8 carry); 
u8 z80BorrowOccured8(u8 op1, u8 op2, u8 carry);
u8 z80HalfBorrowOccured8(u8 op1, u8 op2, u8 carry);

//16 bit carry/borrow check
u8 z80CarryOccured16(u16 op1, u16 op2, u8 carry);
u8 z80HalfCarryOccured16(u16 op1, u16 op2, u8 carry);
u8 z80BorrowOccured16(u16 op1, u16 op2, u8 carry);
u8 z80HalfBorrowOccured16(u16 op1, u16 op2, u8 carry);

void z80WriteU8(struct Z80 *z80, u8 value, u16 address);

u8 z80ReadU8(struct Z80* z80, u16 address);
u8 z80FetchU8(struct Z80 *z80);
u16 z80ReadU16(struct Z80* z80, u16 address);
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
void loadRegHl(struct Z80* z80, u8* reg);
void loadMemReg16(struct Z80* z80, union Register *reg);
void loadMemReg8(struct Z80* z80, u8 reg);
void loadHlReg(struct Z80* z80, u8 reg);
void load16Reg(struct Z80* z80, union Register* reg);
void load16A(struct Z80* z80);
void loadReg(struct Z80* z80, u8* destReg, u8 sourceReg);
void loadRegMem(struct Z80* z80, u8* destReg, union Register* reg);
void loadReg8Mem(struct Z80* z80, union Register mem, u8 reg);
void loadAWithR(struct Z80* z80);
void loadAWithI(struct Z80* z80);
void loadSpReg(struct Z80* z80, union Register* reg);

//Arithmetic
void incReg16(struct Z80* z80, union Register* reg);
void decReg16(struct Z80* z80, union Register* reg);
void decReg8(struct Z80* z80, u8* reg);
void decMemHl(struct Z80* z80);

void incReg8(struct Z80* z80, u8* reg);
void incMemHl(struct Z80* z80);
void addReg16(struct Z80* z80, union Register* destReg, union Register *sourceReg);
void addReg8(struct Z80* z80, u8* destReg, u8 sourceReg);
void addMemHl(struct Z80* z80, u8* destReg);
void adcReg16(struct Z80* z80, union Register* destReg, union Register* sourceReg);
void adcMemHl(struct Z80* z80, u8* destReg);
void adcReg8(struct Z80* z80, u8* destReg, u8 sourceReg);

void subReg8(struct Z80* z80, u8* destReg, u8 sourceReg);
void subMemHl(struct Z80* z80, u8* destReg);
void sbcReg8(struct Z80* z80, u8* destReg, u8 sourceReg);
void sbcMemHl(struct Z80* z80, u8* destReg);
void sbcReg16(struct Z80* z80, union Register* destReg, union Register *sourceReg);

//Branches/Jumps/Returns
void jrImm(struct Z80* z80);
void jrImmCond(struct Z80* z80, u8 cond);
void djnz(struct Z80* z80);
void rst(struct Z80* z80, u8 vector);
void call(struct Z80* z80);
void callCond(struct Z80* z80, u8 cond);
void ret(struct Z80* z80);
void reti(struct Z80* z80);
void retn(struct Z80* z80);
void retCond(struct Z80* z80, u8 cond);
void jp(struct Z80* z80);
void jpCond(struct Z80* z80, u8 cond);
void jpMemHl(struct Z80* z80);

//Logical
void xor(struct Z80* z80, u8 reg);
void xorMemHl(struct Z80* z80);
void or(struct Z80* z80, u8 reg);
void orMemHl(struct Z80* z80);
void and(struct Z80* z80, u8 reg);
void andMemHl(struct Z80* z80);

//Shifts
void rlca(struct Z80* z80);
void rrca(struct Z80* z80);
void rra(struct Z80* z80);
void rla(struct Z80* z80);

//Exchanges
void ex(struct Z80* z80, union Register *reg1, union Register *reg2);
void exx(struct Z80* z80);

//Stack
void push(struct Z80* z80, union Register* reg);
void pop(struct Z80* z80, union Register* reg);

//Io/Ports
void outa(struct Z80* z80);
void outi(struct Z80* z80);
void outd(struct Z80* z80);
void ina(struct Z80* z80);
void out(struct Z80* z80, u8 destPort, u8 sourceReg);
void in(struct Z80* z80, u8 sourcePort, u8* destReg, u8 opcode);

void otir(struct Z80* z80);
void otdr(struct Z80* z80);
void ini(struct Z80* z80);
void ldir(struct Z80* z80);
void ldi(struct Z80* z80);
void ldd(struct Z80* z80);
void lddr(struct Z80* z80);
void rrd(struct Z80* z80);
void rld(struct Z80* z80);

//Misc
void cpl(struct Z80* z80);
void halt(struct Z80* z80);
void cp(struct Z80* z80, u8 reg);
void cpMemHl(struct Z80* z80);
void daa(struct Z80* z80);
void ccf(struct Z80* z80);
void neg(struct Z80* z80);
void scf(struct Z80* z80);
void cpd(struct Z80* z80);
void cpdr(struct Z80* z80);
void cpi(struct Z80* z80);
void cpir(struct Z80* z80);

//Bit instructions
void rlc(struct Z80* z80, u8* reg);
void rlcMemHl(struct Z80* z80);
void rl(struct Z80* z80, u8* reg);
void rlMemHl(struct Z80* z80);
void rrc(struct Z80* z80, u8* reg);
void rrcMemHl(struct Z80* z80);
void rr(struct Z80* z80, u8* reg);
void rrMemHl(struct Z80* z80);
void rrcMemHl(struct Z80* z80);
void sla(struct Z80* z80, u8* reg);
void slaMemHl(struct Z80* z80);
void sra(struct Z80* z80, u8* reg);
void sraMemHl(struct Z80* z80);
void srl(struct Z80* z80, u8* reg);
void srlMemHl(struct Z80* z80);
void sll(struct Z80* z80, u8* reg);
void sllMemHl(struct Z80* z80);

void bit(struct Z80* z80, u8 reg, u8 bit);
void bitMemHl(struct Z80* z80, u8 bitToTest);
void res(struct Z80* z80, u8* reg, u8 bit);
void resMemHl(struct Z80* z80, u8 bit);
void set(struct Z80* z80, u8* reg, u8 bit);
void setMemHl(struct Z80* z80, u8 bit);

//Ix instructions
void loadRegIx(struct Z80* z80, u8* reg);
void loadIxReg(struct Z80* z80, u8 reg);
void loadIxImm(struct Z80* z80);

void incMemIx(struct Z80* z80);
void addMemIx(struct Z80* z80, u8* reg);
void adcMemIx(struct Z80* z80, u8* reg);
void decMemIx(struct Z80* z80);
void subMemIx(struct Z80* z80, u8* reg);
void sbcMemIx(struct Z80* z80, u8* reg);

void andMemIx(struct Z80* z80);
void xorMemIx(struct Z80* z80);
void orMemIx(struct Z80* z80);
void cpMemIx(struct Z80* z80);

//Ix branches
void jpMemIx(struct Z80* z80);

//Bit ix instructions
void bitMemIx(struct Z80* z80, u8 bitToTest);
void resMemIx(struct Z80* z80, u8 bit);
void setMemIx(struct Z80* z80, u8 bit);
void rlcMemIx(struct Z80* z80);
void rrcMemIx(struct Z80* z80);
void rlMemIx(struct Z80* z80);
void rrMemIx(struct Z80* z80);
void slaMemIx(struct Z80* z80);
void sraMemIx(struct Z80* z80);
void srlMemIx(struct Z80* z80);
void sllMemIx(struct Z80* z80);


//Iy instructions
void loadIyImm(struct Z80* z80);
void loadRegIy(struct Z80* z80, u8* reg);
void loadIyReg(struct Z80* z80, u8 reg);

void incMemIy(struct Z80* z80);
void decMemIy(struct Z80* z80);
void addMemIy(struct Z80* z80, u8* reg);
void adcMemIy(struct Z80* z80, u8* reg);
void subMemIy(struct Z80* z80, u8* reg);
void sbcMemIy(struct Z80* z80, u8* reg);

void andMemIy(struct Z80* z80);
void xorMemIy(struct Z80* z80);
void orMemIy(struct Z80* z80);
void cpMemIy(struct Z80* z80);

void jpMemIy(struct Z80* z80);

//Bit iy instructions
void bitMemIy(struct Z80* z80, u8 bitToTest);
void resMemIy(struct Z80* z80, u8 bit);
void setMemIy(struct Z80* z80, u8 bit);
void rlcMemIy(struct Z80* z80);
void rrcMemIy(struct Z80* z80);
void rlMemIy(struct Z80* z80);
void rrMemIy(struct Z80* z80);
void slaMemIy(struct Z80* z80);
void sraMemIy(struct Z80* z80);
void srlMemIy(struct Z80* z80);
void sllMemIy(struct Z80* z80);


//Interrupt related instructions
void di(struct Z80* z80);
void ei(struct Z80* z80);
void im(struct Z80* z80, enum IntMode interruptMode);
