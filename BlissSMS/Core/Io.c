#include "Io.h"

void ioInit(struct Io* io)
{
	io->memoryControl = 0x0;
	io->ioControl = 0x0;
	io->vcounter = 0x0;
	io->hcounter = 0x0;
	io->vdpData = 0x0;
	io->vdpControl = 0x0;
	io->ioAB = 0x0;
	io->ioBMisc = 0x0;
}

void ioWriteU8(struct Io* io, u8 value, u8 address)
{
	u8 even_address = ((address & 0x1) == 0);
	if (address >= 0x0 && address <= 0x3F) {
		if (even_address)
			io->memoryControl = value;
		else
			io->ioControl = value;
	}
	else if (address >= 0x40 && address <= 0x7F) {
		//writes to here goes to the psg
	}
	else if (address >= 0x80 && address <= 0xBF) {
		if (even_address)
			io->vdpData = value;
		else
			io->vdpControl = value;
	}
}

u8 ioReadU8(struct Io* io, u8 address)
{
	u8 even_address = ((address & 0x1) == 0);
	if (address >= 0x40 && address <= 0x7F) {
		if (even_address)
			return io->vcounter;
		else
			return io->hcounter;
	}
	else if (address >= 0x80 && address <= 0xBF) {
		if (even_address)
			return io->vdpData;
		else
			return io->vdpControl;
	}
	else if (address >= 0xC0 && address <= 0xFF) {
		if (even_address)
			return io->ioAB;
		else
			return io->ioBMisc;
	}
}
