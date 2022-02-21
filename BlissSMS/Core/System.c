#include "System.h"

void systemInit(struct System* sys)
{
	ioInit(&sys->io);
	memoryBusInit(&sys->bus);
	memoryBusLoadBios(&sys->bus, "test_roms/bios13fx.sms");
	memoryBusConnectIo(&sys->io);

	cartInit(&sys->cart);
	cartLoad(&sys->cart, "test_roms/zexdoc.sms");

	memoryBusLoadCartridge(&sys->bus, &sys->cart);

	z80Init(&sys->z80);
	z80ConnectBus(&sys->bus);
	z80ConnectIo(&sys->io);

	sys->running = 1;
}

void systemRunEmulation(struct System* sys)
{
	if (sys->running) {
		s32 cycles_this_frame = 0;
		while (cycles_this_frame < MAX_CYCLES_PER_FRAME) {
			u16 cycles = z80Clock(&sys->z80);
			cycles_this_frame += cycles;

			//vdpUpdate(cycles);
			//psgUpdate(cycles);
			if (sys->z80.process_interrupt_delay)
				continue;

			z80HandleInterrupts(&sys->z80);
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
