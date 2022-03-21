#pragma once
#include "Bus.h"
#include "Io.h"
#include "Z80.h"
#include "Vdp.h"
#include "Joypad.h"
#include "Cart.h"
#include "Log.h"

#define CPU_CLOCK 3579545
#define MAX_CYCLES_PER_FRAME CPU_CLOCK / 60
#define SCANLINES_PER_FRAME 262
#define NTSC_FPS 60
#define CYCLES_PER_SCANLINE CPU_CLOCK / SCANLINES_PER_FRAME / NTSC_FPS //227 cycles

struct System {
	struct Bus bus;
	struct Io io;
	struct Z80 z80;
	struct Vdp vdp;
	struct Joypad joy;
	struct Cart cart;

	struct Log log;

	u8 running;
	u8 run_debugger;
};

void systemInit(struct System* sys);
void systemRunEmulation(struct System* sys);
void systemRenderGraphics(struct System* sys, sfRenderWindow *window);
void systemFree(struct System* sys);

void tickCpu(struct System* sys);