#pragma once
#include "Util.h"

#define SRAM_SIZE 0x4000
#define CART_32K 0x8000
#define CART_64K 0x10000
#define CART_128K 0x20000
#define CART_256K 0x40000

struct Cart {
	u8* memory;
	u8 ram_banks[0x8000]; //on board cartridge ram
	u8 sram[SRAM_SIZE];
	u8 region;
	u32 romsize;
};

void cartInit(struct Cart* cart);
void cartLoad(struct Cart* cart, const char* path);

void cartWriteU8(struct Cart* cart, u8 value, u32 address);
u8 cartReadU8(struct Cart* cart, u32 address, u8 ram);

void cartFree(struct Cart* cart);