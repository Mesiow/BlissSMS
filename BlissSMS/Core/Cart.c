#include "Cart.h"

void cartInit(struct Cart* cart)
{
	cart->memory = NULL;
	cart->uses_sram = 0;
	memset(cart->ram_banks, 0x0, 0x8000);
}

void cartLoad(struct Cart* cart, char* path)
{
	char* ext = ".sms";
	s32 str_size = strlen(path) + strlen(ext) + 1;

	char* dest = (u8*)malloc(str_size);
	if (dest != NULL) {
		memset(dest, 0, str_size);
		strcat(dest, path);
		strcat(dest, ext);

		FILE* rom = fopen(dest, "r");
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
		if (file_size == CART_32K || file_size == CART_64K
			|| file_size == CART_128K || file_size == CART_256K
			|| file_size == CART_512K) {
			cart->memory = (u8*)malloc(file_size * sizeof(u8));
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
		free(dest);

		//does the cart use sram?
		char* sav_ext = ".sav";
		dest = (u8*)malloc(str_size);

		if (dest != NULL) {
			s32 str_size = strlen(path) + strlen(sav_ext) + 1;
			memset(dest, 0, str_size);
			strcat(dest, path);
			strcat(dest, sav_ext);

			FILE* sav = fopen(dest, "r");
			if (sav != NULL) {
				cart->uses_sram = 1;
				printf("cart uses sram\n");

				fclose(sav);
				free(dest);
			}
		}
	}
}

void cartWriteU8(struct Cart* cart, u8 value, u32 address)
{
	cart->ram_banks[address] = value;
}

u8 cartReadU8(struct Cart* cart, u32 address, u8 ram)
{
	if (ram)
		return cart->ram_banks[address];

	return cart->memory[address];
}

void cartFree(struct Cart* cart)
{
	if (cart != NULL) {
		if (cart->memory != NULL)
			free(cart->memory);
	}
}
