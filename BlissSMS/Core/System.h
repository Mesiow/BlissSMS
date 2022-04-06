#pragma once
#include "Bus.h"
#include "Io.h"
#include "Z80.h"
#include "Vdp.h"
#include "Psg.h"
#include "Joypad.h"
#include "Cart.h"
#include "Log.h"

#define CPU_CLOCK 3579545
#define SCANLINES_PER_FRAME 262
#define FPS 60
#define CYCLES_PER_SCANLINE 228
#define MAX_CYCLES_PER_FRAME SCANLINES_PER_FRAME * CYCLES_PER_SCANLINE


struct System {
	struct Bus bus;
	struct Io io;
	struct Z80 z80;
	struct Vdp vdp;
	struct Psg psg;
	struct Joypad joy;
	struct Cart cart;

	struct Log log;

	u8 running;
	u8 run_debugger;
};

void systemInit(struct System* sys);
void systemRunEmulation(struct System* sys);
void systemRenderGraphics(struct System* sys, sfRenderWindow *window);
void systemHandleInput(struct System *sys, sfEvent* ev);
void systemFree(struct System* sys);

void tickCpu(struct System* sys);