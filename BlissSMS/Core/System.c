#include "System.h"

void systemInit(struct System* sys)
{
	//Memory init
	ioInit(&sys->io);
	memoryBusInit(&sys->bus);

	//Cpu init
	z80Init(&sys->z80);
	z80ConnectBus(&sys->z80, &sys->bus);
	z80ConnectIo(&sys->z80, &sys->io);

	//Vdp init
	vdpInit(&sys->vdp);
	vdpConnectIo(&sys->vdp, &sys->io);

	cartInit(&sys->cart);
	cartLoad(&sys->cart, "roms/Astro Flash (Japan).sms");

	memoryBusLoadCartridge(&sys->bus, &sys->cart);

	sys->running = 1;
}

void systemRunEmulation(struct System* sys)
{
	struct Vdp* vdp = &sys->vdp;
	struct Z80* z80 = &sys->z80;

	if (sys->running) {
		s32 cycles_this_frame = 0;
		while (cycles_this_frame < MAX_CYCLES_PER_FRAME) {
			u16 cycles = z80Clock(z80);
			cycles_this_frame += cycles;

			vdpUpdate(vdp, cycles);
			//psgUpdate(cycles);
			if (sys->z80.process_interrupt_delay)
				continue;

			z80HandleInterrupts(z80, vdp);
		}
		//update joypad
		//update graphics buffer
	}
}

void systemFree(struct System* sys)
{
	cartFree(&sys->cart);
}

void tickCpu(struct System* sys)
{
	u16 cpu_cycles = z80Clock(&sys->z80);
}
