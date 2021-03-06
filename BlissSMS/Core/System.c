#include "System.h"

void systemInit(struct System* sys)
{
	ioInit(&sys->io);
	memoryBusInit(&sys->bus);
	memoryBusLoadBios(&sys->bus, "test_roms/bios13fx.sms");
	ioConnectBus(&sys->io, &sys->bus);

	z80Init(&sys->z80);
	z80ConnectBus(&sys->z80, &sys->bus);
	z80ConnectIo(&sys->z80, &sys->io);
	
	vdpInit(&sys->vdp);
	vdpConnectIo(&sys->vdp, &sys->io);
	ioConnectVdp(&sys->io, &sys->vdp);
	sys->vdp.sys = &sys;

	psgInit(&sys, &sys->psg);
	ioConnectPsg(&sys->io, &sys->psg);

	joypadInit(&sys->joy);
	ioConnectJoypad(&sys->io, &sys->joy);

	cartInit(&sys->cart);
	
	//Working games
	//cartLoad(&sys->cart, "test_roms/VDPTEST");
	//cartLoad(&sys->cart, "roms/Arcade Smash Hits (Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Andre Agassi Tennis (Europe)"); boots and runs
	//cartLoad(&sys->cart, "roms/Alex Kidd - The Lost Stars (World)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Alex Kidd in Miracle World (USA, Europe) (Rev 1)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Alex Kidd - High-Tech World (USA, Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Air Rescue (Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/After Burner (World)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Aerial Assault (USA)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Spider-Man - Return of the Sinister Six (Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/R-Type (World)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Alien 3"); //boots and runs
	//cartLoad(&sys->cart, "roms/Aladdin (Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Alien Storm (Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Sagaia (Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Golvellius (USA, Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Shinobi (USA, Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Fantasy Zone (World) (Rev 2)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Rastan (USA, Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Enduro Racer (USA, Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Altered Beast (USA, Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Astro Warrior (Japan, USA)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Sonic The Hedgehog 2 (Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Sonic Chaos (Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Sonic Spinball (Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/World Grand Prix"); //boots and runs
	//cartLoad(&sys->cart, "roms/Zillion (USA)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Astro Flash (Japan)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Spider-Man vs. The Kingpin (USA, Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Sonic The Hedgehog (USA, Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Ninja Gaiden (Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Teddy Boy (USA, Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Golden Axe Warrior (USA, Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Road Rash (Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Out Run (World)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Master of Darkness (Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Phantasy Star (USA, Europe) (Rev 2)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Marble Madness (Europe)"); //boots and runs
	//cartLoad(&sys->cart, "roms/Chase HQ (J) [!]"); //boots and runs
	memoryBusLoadCart(&sys->bus, &sys->cart);

	sys->running = 1;
	sys->run_debugger = 0;
}

void systemRunEmulation(struct System* sys)
{
	if (sys->running) {
		struct Vdp* vdp = &sys->vdp;
		struct Psg* psg = &sys->psg;
		struct Z80* z80 = &sys->z80;
		struct Joypad* joy = &sys->joy;

		s32 cycles_this_frame = 0;
		while (!vdpFrameComplete(vdp)) {
			u16 cycles = z80Clock(z80);
			cycles_this_frame += cycles;

			vdpUpdate(vdp, cycles);
			psgUpdate(psg, cycles);
			if (sys->z80.process_interrupt_delay)
				continue;

			z80HandleInterrupts(z80, vdp);
		}
		joypadUpdate(joy);
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
		if (ev->key.code == sfKeySpace) {
			sys->z80.service_nmi = 1;
		}
		if (ev->key.code == sfKeyTab) {
			ioResetButtonPressed(&sys->io, 1);
		}
	}
	else if (ev->type == sfEvtKeyReleased) {
		if (ev->key.code == Up) joypadButtonPressed(joy, Up, 0);
		if (ev->key.code == Down) joypadButtonPressed(joy, Down, 0);
		if (ev->key.code == Left) joypadButtonPressed(joy, Left, 0);
		if (ev->key.code == Right) joypadButtonPressed(joy, Right, 0);
		if (ev->key.code == A) joypadButtonPressed(joy, A, 0);
		if (ev->key.code == B) joypadButtonPressed(joy, B, 0);
		if (ev->key.code == sfKeyTab) {
			ioResetButtonPressed(&sys->io, 0);
		}
	}
}

void systemFree(struct System* sys)
{
	cartDumpSram(&sys->cart);
	cartFree(&sys->cart);
	vdpFree(&sys->vdp);
	psgFree(&sys->psg);
}

void tickCpu(struct System* sys)
{
	u16 cpu_cycles = z80Clock(&sys->z80);
}

void systemSetApuCallback(struct System* sys, sms_apu_callback cb, void* user, u32 freq)
{
	sys->apu_callback = cb;
	sys->apu_callback_freq = freq;
	sys->apu_callback_counter = 0;
}
