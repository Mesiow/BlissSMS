#include "Bus.h"
#include "Io.h"
#include "Cart.h"

void memoryBusInit(struct Bus* bus)
{
	memset(bus->memory, 0x0, 0xC000);
	memset(bus->system_ram, 0x0, SYSRAM_SIZE);
	memset(bus->bios, 0x0, BIOS_SIZE);

	bus->wram_enabled = 0;
	bus->cart_slot_enabled = 0;
	bus->bios_enabled = 0;
	bus->io_enabled = 0;
	bus->cart_loaded = 0;

	bus->rom_bank0_register = 0;
	bus->rom_bank1_register = 1;
	bus->rom_bank2_register = 2;
}

void memoryBusLoadBios(struct Bus* bus, const char *path)
{
	FILE* bios = fopen(path, "r");
	if (bios == NULL) {
		printf("---Bios file could not be found---\n");
		return;
	}
	bus->bios_enabled = 1;
	fread(bus->bios, sizeof(u8), BIOS_SIZE, bios);
	fclose(bios);
}

void memoryBusLoadCart(struct Bus* bus, struct Cart* cart)
{
	bus->cart = cart;
	bus->cart_loaded = 1;
	memcpy(bus->memory, cart->memory, 0xC000);
}

void memoryBusWriteU8(struct Bus* bus, u8 value, u16 address)
{	
	if (address >= SYSRAM_START && address <= SYSRAM_END) {
		bus->system_ram[address & (SYSRAM_SIZE - 1)] = value;
	}
	//Mirrored system ram
	else if (address >= 0xE000 && address <= 0xFFFF) {
		bus->system_ram[address & (SYSRAM_SIZE - 1)] = value;
	}

	//Rom mapping registers
	if (address == 0xFFFD) {
		bus->rom_bank0_register = value;
	}
	else if (address == 0xFFFE) {
		bus->rom_bank1_register = value;
	}
	else if (address == 0xFFFF) {
		bus->rom_bank2_register = value;
	}
}

void writeMemoryControl(struct Bus* bus, u8 value)
{
	bus->cart_slot_enabled = ((value >> 6) & 0x1) == 0;
	bus->wram_enabled = ((value >> 4) & 0x1) == 0;
	bus->bios_enabled = ((value >> 3) & 0x1) == 0;
	bus->io_enabled = ((value >> 2) & 0x1) == 0;
}

u8 memoryBusReadU8(struct Bus* bus, u16 address)
{
	if (address < BIOS_SIZE && bus->bios_enabled) {
		return bus->bios[address & (BIOS_SIZE - 1)];
	}

	if (bus->cart_slot_enabled) {
	
		if (address >= ROM_START && address <= ROM_END) {
			return bus->memory[address];
		}

		if (bus->cart->romsize == CART_32K) {
			if(address < 0x8000)
				return bus->memory[address & 0x7FFF];
		}
		else {
			if (address >= ROM_SLOT_0_START && address <= ROM_SLOT_0_END) {
				return memoryBusHandleRomMappingRead(bus, address, 0);
			}
			else if (address >= ROM_SLOT_1_START && address <= ROM_SLOT_1_END) {
				return memoryBusHandleRomMappingRead(bus, address, 1);
			}
			else if (address >= RAM_SLOT_2_START && address <= RAM_SLOT_2_END) {
				return memoryBusHandleRomMappingRead(bus, address, 2);
			}
		}
	}
	
	if (address >= SYSRAM_START && address <= SYSRAM_END) {
		return bus->system_ram[address & (SYSRAM_SIZE - 1)];
	}

	//Mirrored system ram
	else if (address >= 0xE000 && address <= 0xFFFF) {
		return bus->system_ram[address & (SYSRAM_SIZE - 1)];
	}
}

u8 memoryBusHandleRomMappingRead(struct Bus* bus, u16 address, u8 romBank)
{
	switch (romBank) {
		case 0: {
			u32 bank_address = address + (0x4000 * bus->rom_bank0_register);
			return cartReadU8(bus->cart, bank_address);
		}
		break;
		case 1: {
			u32 bank_address = address + (0x4000 * bus->rom_bank1_register);
			bank_address -= 0x4000; //get actual bank offset
			return cartReadU8(bus->cart, bank_address);
		}
		break;
		case 2: {
			u32 bank_address = address + (0x4000 * bus->rom_bank2_register);
			bank_address -= 0x8000;
			return cartReadU8(bus->cart, bank_address);
		}
		break;
		default:
			printf("Rom bank %d not yet implemented\n", romBank);
			break;
	}
}

