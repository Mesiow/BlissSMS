#pragma once
#include "Bus.h"
#include "Z80.h"

#define CYCLES_PER_SCANLINE 3420
#define SCANLINES_PER_FRAME 243
#define MAX_CYCLES_PER_FRAME CYCLES_PER_SCANLINE * SCANLINES_PER_FRAME

struct System {
	struct Bus bus;
	struct Z80 z80;
	u8 running;
};

void systemInit(struct System* sys);
void systemRunEmulation(struct System* sys);