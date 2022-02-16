#include "System.h"

void systemInit(struct System* sys)
{
	memoryBusInit(&sys->bus);
	memoryBusLoadBios(&sys->bus, "test_roms/bios13fx.sms");
	ioInit(&sys->io);

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

			//vdp.update(cycles);
			//psg.update(cycles);
			//z80HandleInterrupts(&sys->z80)

			cycles_this_frame += cycles;
		}
		//update joypad
		//update graphics buffer
	}
}

void tickCpu(struct System* sys)
{
	u16 cpu_cycles = z80Clock(&sys->z80);
}
