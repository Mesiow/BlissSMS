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


struct ApuCallbackData {
	s8 tone0;
	s8 tone1;
	s8 tone2;
	s8 noise;
};


typedef void (*sms_apu_callback)(void* user, struct ApuCallbackData* data);


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

	sms_apu_callback apu_callback;
	u32 apu_callback_freq;
	s32 apu_callback_counter;
};

void systemInit(struct System* sys);
void systemRunEmulation(struct System* sys);
void systemRenderGraphics(struct System* sys, sfRenderWindow *window);
void systemHandleInput(struct System *sys, sfEvent* ev);
void systemFree(struct System* sys);

void tickCpu(struct System* sys);

void systemSetApuCallback(struct System* sys, sms_apu_callback cb, void* user, u32 freq);