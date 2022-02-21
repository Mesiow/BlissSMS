#pragma once
#include "Util.h"

#define SRAM_SIZE 0x4000

struct Cart {
	u8* memory;
	u8 sram[SRAM_SIZE];
	u8 region;
	u32 romsize;
};

void cartInit(struct Cart* cart);
void cartLoad(struct Cart* cart, const char* path);
void cartFree(struct Cart* cart);