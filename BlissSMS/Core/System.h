#pragma once
#include "Bus.h"
#include "Io.h"
#include "Z80.h"
#include "Cart.h"

#define CYCLES_PER_SCANLINE 3420
#define SCANLINES_PER_FRAME 259
#define MAX_CYCLES_PER_FRAME CYCLES_PER_SCANLINE * SCANLINES_PER_FRAME

struct System {
	struct Bus bus;
	struct Io io;
	struct Z80 z80;
	struct Cart cart;
	u8 running;
};

void systemInit(struct System* sys);
void systemRunEmulation(struct System* sys);
void systemFree(struct System* sys);

void tickCpu(struct System* sys);