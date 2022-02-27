#include "Bus.h"
#include "Io.h"
#include "Cart.h"

void memoryBusInit(struct Bus* bus)
{
	memset(bus->rom, 0x0, ROM_SIZE);
	memset(bus->romslot0, 0x0, ROM_MAPPER_0_SIZE);
	memset(bus->romslot1, 0x0, ROM_MAPPER_1_SIZE);
	memset(bus->ramslot2, 0x0, RAM_MAPPER_2_SIZE);
	memset(bus->systemRam, 0x0, SYSRAM_SIZE);
	memset(bus->bios, 0x0, BIOS_SIZE);

	bus->biosEnabled = 0;
}

void memoryBusLoadBios(struct Bus* bus, const char *path)
{
	FILE* bios = fopen(path, "r");
	if (bios == NULL) {
		printf("---Bios file could not be found---\n");
		return;
	}
	fread(bus->bios, sizeof(u8), BIOS_SIZE, bios);
	fclose(bios);
}

void memoryBusLoadCartridge(struct Bus* bus, struct Cart* cart)
{
	bus->cart = cart;
	//32k roms do not use paging hardware and simply map from 0 - 0x8000
	//64k and larger roms map their first 48k from 0 - 0xC000,
	//but each 16k page can be remapped to different interal rom banks via
	//paging registers
	switch (cart->romsize) {
		case CART_32K: {
			//Map cart to rom from 0x0 - 0x8000 (32k)
			memcpy(bus->rom, cart->memory, ROM_SIZE);
			memcpy(bus->romslot0, cart->memory + ROM_SIZE, ROM_MAPPER_0_SIZE);
			memcpy(bus->romslot1, cart->memory + ROM_MAPPER_0_SIZE, ROM_MAPPER_1_SIZE);
		}
		break;
		case CART_64K:
		case CART_128K:
		case CART_256K: {
			//Map cart to rom from 0x0 - 0xC000 (48k)
			memcpy(bus->rom, cart->memory, ROM_SIZE);
			memcpy(bus->romslot0, cart->memory + ROM_SIZE, ROM_MAPPER_0_SIZE);
			memcpy(bus->romslot1, cart->memory + ROM_MAPPER_0_SIZE, ROM_MAPPER_1_SIZE);
			memcpy(bus->ramslot2, cart->memory + ROM_MAPPER_1_SIZE, RAM_MAPPER_2_SIZE);
		}
		break;
	}
}

void memoryBusWriteU8(struct Bus* bus, u8 value, u16 address)
{	
	if (address >= RAM_SLOT_2_START && address <= RAM_SLOT_2_END) {
		bus->ramslot2[address & (RAM_MAPPER_2_SIZE - 1)] = value;
	}
	else if (address >= SYSRAM_START && address <= SYSRAM_END) {
		bus->systemRam[address & (SYSRAM_SIZE - 1)] = value;
	}
	//Mirrored system ram
	else if (address >= 0xE000 && address <= 0xFFFF) {
		bus->systemRam[address & (SYSRAM_SIZE - 1)] = value;
	}
}

u8 memoryBusReadU8(struct Bus* bus, u16 address)
{
	//u8 bios_enabled = (((ioBus->memoryControl >> 3) & 0x1) == 0x0);
	if (bus->biosEnabled) {
		if (address >= ROM_START && address <= (BIOS_SIZE - 1)) {
			return bus->bios[address & (BIOS_SIZE - 1)];
		}
	}

	if (address >= ROM_START && address <= ROM_END) {
		return bus->rom[address & (ROM_SIZE - 1)];
	}
	else if (address >= ROM_SLOT_0_START && address <= ROM_SLOT_0_END) {
		return bus->romslot0[address & (ROM_MAPPER_0_SIZE - 1)];
	}
	else if (address >= ROM_SLOT_1_START && address <= ROM_SLOT_1_END) {
		return bus->romslot1[address & (ROM_MAPPER_1_SIZE - 1)];
	}
	else if (address >= RAM_SLOT_2_START && address <= RAM_SLOT_2_END) {
		return bus->ramslot2[address & (RAM_MAPPER_2_SIZE - 1)];
	}
	else if (address >= SYSRAM_START && address <= SYSRAM_END) {
		return bus->systemRam[address & (SYSRAM_SIZE - 1)];
	}

	//Mirrored system ram
	else if (address >= 0xE000 && address <= 0xFFFF) {
		return bus->systemRam[address & (SYSRAM_SIZE - 1)];
	}

	return 0x0;
}

u8 memoryBusReadRamMapper(struct Bus* bus)
{
	return bus->systemRam[RAM_MAPPING_REGISTER & (SYSRAM_SIZE - 1)];
}

u8 memoryBusReadRomBankRegister(struct Bus* bus, u16 rombank)
{
	return bus->systemRam[rombank & (SYSRAM_SIZE - 1)];
}
