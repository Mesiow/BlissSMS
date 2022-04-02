#include "Io.h"
#include "Vdp.h"
#include "Joypad.h"
#include "Bus.h"

void ioInit(struct Io* io)
{
	io->vdp = NULL;
	io->bus = NULL;
}

void ioConnectVdp(struct Io* io, struct Vdp* vdp)
{
	io->vdp = vdp;
}

void ioConnectJoypad(struct Io* io, struct Joypad* joy)
{
	io->joy = joy;
}

void ioConnectBus(struct Io* io, struct Bus* bus)
{
	io->bus = bus;
}

void ioWriteU8(struct Io* io, u8 value, u8 address)
{
	u8 even_address = ((address & 0x1) == 0);
	if (address >= 0x0 && address <= 0x3F) {
		if (even_address)
			writeMemoryControl(io->bus, value);
		else {
			io->nationalization_port = value;
			//copy bits 5 and 7 to joypad port 2 so
			//they can be read back from
			u8 b5 = (io->nationalization_port >> 5) & 0x1;
			u8 b7 = (io->nationalization_port >> 7) & 0x1;

			if (b5) io->joy->joypad_port2 |= (1 << 6);
			else io->joy->joypad_port2 &= ~(1 << 6);

			if(b7) io->joy->joypad_port2 |= (1 << 7);
			else io->joy->joypad_port2 &= ~(1 << 7);
		}
	}
	else if (address >= 0x40 && address <= 0x7F) {
		//writes to here goes to the psg
	}
	else if (address >= 0x80 && address <= 0xBF) {
		if (even_address)
			vdpWriteDataPort(io->vdp, value);
		else
			vdpWriteControlPort(io->vdp, value);
	}
}

u8 ioReadU8(struct Io* io, u8 address)
{
	u8 even_address = ((address & 0x1) == 0);
	if (address >= 0x40 && address <= 0x7F) {
		if (even_address)
			return io->vdp->vcount_port;
		else
			return io->vdp->hcounter;
	}
	else if (address >= 0x80 && address <= 0xBF) {
		if (even_address)
			return vdpReadDataPort(io->vdp);
		else
			return vdpReadControlPort(io->vdp);
	}
	else if (address >= 0xC0 && address <= 0xFF) {
		if (even_address)
			return io->joy->joypad_port;
		else
			return io->joy->joypad_port2;
	}
}
