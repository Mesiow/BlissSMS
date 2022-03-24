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

	cartInit(&sys->cart);
	//cartLoad(&sys->cart, "roms/Astro Flash (Japan).sms");
	//cartLoad(&sys->cart, "roms/Sonic The Hedgehog (USA, Europe).sms");
	//cartLoad(&sys->cart, "test_roms/Not_Only_Words.sms");
	cartLoad(&sys->cart, "roms/Teddy Boy (USA, Europe).sms");
	memoryBusLoadCart(&sys->bus, &sys->cart);

	sys->running = 1;
	sys->run_debugger = 0;
}

void systemRunEmulation(struct System* sys)
{
	if (sys->running) {
		struct Vdp* vdp = &sys->vdp;
		struct Z80* z80 = &sys->z80;

		s32 cycles_this_frame = 0;
		while (!vdpFrameComplete(vdp)) {
			u16 cycles = z80Clock(z80);
			cycles_this_frame += cycles;

			vdpUpdate(vdp, cycles);
			if (sys->z80.process_interrupt_delay)
				continue;

			z80HandleInterrupts(z80, vdp);
		}
		vdpBufferPixels(vdp);
	}
}

void systemRenderGraphics(struct System* sys, sfRenderWindow *window)
{
	vdpDisplayGraphics(&sys->vdp, window);
}

void systemHandleInput(struct System *sys, sfEvent* ev)
{
	struct Joypad* joy = &sys->joy;
	if (ev->type == sfEvtKeyPressed) {
		if (ev->key.code == Up) joypadButtonPressed(joy, Up, 1);
		if (ev->key.code == Down) joypadButtonPressed(joy, Down, 1);
		if (ev->key.code == Left) joypadButtonPressed(joy, Left, 1);
		if (ev->key.code == Right) joypadButtonPressed(joy, Right, 1);
		if (ev->key.code == A) joypadButtonPressed(joy, A, 1);
		if (ev->key.code == B) joypadButtonPressed(joy, B, 1);
	}
	else if (ev->type == sfEvtKeyReleased) {
		if (ev->key.code == Up) joypadButtonPressed(joy, Up, 0);
		if (ev->key.code == Down) joypadButtonPressed(joy, Down, 0);
		if (ev->key.code == Left) joypadButtonPressed(joy, Left, 0);
		if (ev->key.code == Right) joypadButtonPressed(joy, Right, 0);
		if (ev->key.code == A) joypadButtonPressed(joy, A, 0);
		if (ev->key.code == B) joypadButtonPressed(joy, B, 0);
	}
}

void systemFree(struct System* sys)
{
	//cartFree(&sys->cart);
	vdpFree(&sys->vdp);
}

void tickCpu(struct System* sys)
{
	u16 cpu_cycles = z80Clock(&sys->z80);
}
