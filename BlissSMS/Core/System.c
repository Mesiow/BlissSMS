#include "System.h"

void systemInit(struct System* sys)
{
	//Memory init
	ioInit(&sys->io);
	memoryBusInit(&sys->bus);
	memoryBusLoadBios(&sys->bus, "test_roms/bios13fx.sms");
	ioConnectBus(&sys->io, &sys->bus);

	//Cpu init
	z80Init(&sys->z80);
	z80ConnectBus(&sys->z80, &sys->bus);
	z80ConnectIo(&sys->z80, &sys->io);
	//cpmLoadRom(&sys->z80, "test_roms/prelim.com"); //pass
	//cpmLoadRom(&sys->z80, "test_roms/zexdoc.cim"); //pass

	//Vdp init
	vdpInit(&sys->vdp);
	vdpConnectIo(&sys->vdp, &sys->io);
	ioConnectVdp(&sys->io, &sys->vdp);

	joypadInit(&sys->joy);
	ioConnectJoypad(&sys->io, &sys->joy);

	//cartInit(&sys->cart);
	//cartLoad(&sys->cart, "roms/Astro Flash (Japan).sms");
	//memoryBusLoadCartridge(&sys->bus, &sys->cart);

	sys->running = 1;
	sys->run_debugger = 0;
}

void systemRunEmulation(struct System* sys)
{
	if (sys->running) {
		struct Vdp* vdp = &sys->vdp;
		struct Z80* z80 = &sys->z80;

		s32 cycles_this_frame = 0;
		while (cycles_this_frame < MAX_CYCLES_PER_FRAME) {
			if (sys->run_debugger)
				compareAgainstLog(&sys->log, z80);

			u16 cycles = z80Clock(z80);
			cycles_this_frame += cycles;

			vdpUpdate(vdp, cycles);
			//psgUpdate(cycles);
			if (sys->z80.process_interrupt_delay)
				continue;

			z80HandleInterrupts(z80, vdp);
		}
		//update joypad
		vdpBufferPixels(vdp);
	}
}

void systemRenderGraphics(struct System* sys, sfRenderWindow *window)
{
	vdpDisplayGraphics(&sys->vdp, window);
}

void systemFree(struct System* sys)
{
	//cartFree(&sys->cart);
}

void tickCpu(struct System* sys)
{
	u16 cpu_cycles = z80Clock(&sys->z80);
}
