#include "Cart.h"

void cartInit(struct Cart* cart)
{
	cart->memory = NULL;
	memset(cart->sram, 0x0, SRAM_SIZE);
}

void cartLoad(struct Cart* cart, const char* path)
{
	FILE* rom = fopen(path, "r");
	if (rom == NULL) {
		printf("---Cartridge file: %s could not be found---\n", path);
		return;
	}
	//Determine rom size
	fseek(rom, 0, SEEK_END);
	u32 file_size = ftell(rom);
	fseek(rom, 0, SEEK_SET);

	cart->romsize = file_size;
	//32kb, 64kb, 128kb, 256kb
	if (file_size == 0x8000 || file_size == 0x10000
		|| file_size == 0x20000 || file_size == 0x40000) {
		cart->memory = (u8*)malloc(file_size * sizeof(u8));
		printf("cart size: 0x%05X\n", file_size);
	}
	else {
		printf("--Cartridge size: 0x%05X not supported!--\n", file_size);
		return;
	}

	if (cart->memory != NULL) {
		fread(cart->memory, sizeof(u8), file_size, rom);

		u16 header_start_offset = 0x7FF0;

		u8 region_size = cart->memory[header_start_offset + 0xF];
		cart->region = (region_size >> 4) & 0xF;
	}
	fclose(rom);
}

void cartFree(struct Cart* cart)
{
	if (cart->memory != NULL)
		free(cart->memory);
}
