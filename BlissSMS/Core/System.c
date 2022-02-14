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
			u16 cpu_cycles = z80Clock(&sys->z80);

			//the main system clock is 3x faster than the z80 clock
			s32 machine_cycles = cpu_cycles * 3;
			//vdp clock is half the speed of the main system clock
			s32 vdp_cycles = machine_cycles / 2;

			//tick timers
			//tick vdp (graphics unit)
			//tick sound unit
			//check for interrupts

			cycles_this_frame += machine_cycles;
		}
		//update joypad
		//update graphics buffer
	}
}

void tickCpu(struct System* sys)
{
	u16 cpu_cycles = z80Clock(&sys->z80);
}
