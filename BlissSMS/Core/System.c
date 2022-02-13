#include "System.h"

void systemInit(struct System* sys)
{
	memoryBusInit(&sys->bus);
	memoryBusLoadBios(&sys->bus, "test_roms/bios13fx.sms");

	z80Init(&sys->z80);
	z80ConnectBus(&sys->bus);

	sys->running = 1;
}

void systemRunEmulation(struct System* sys)
{
	if (sys->running) {
		s32 cycles_this_frame = 0;
		while (cycles_this_frame < MAX_CYCLES_PER_FRAME) {
			u8 cycles = z80Clock(&sys->z80);
			cycles_this_frame += cycles;

			//tick timers
			//tick vdp (graphics unit)
			//check for interrupts
		}
		//update joypad
		//update graphics buffer
	}
}
