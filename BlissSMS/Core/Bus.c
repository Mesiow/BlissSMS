#include "Bus.h"

void memoryBusInit(struct Bus* bus)
{
	memset(bus->rom, 0x0, ROM_SIZE);
	memset(bus->romslot0, 0x0, ROM_MAPPER_0_SIZE);
	memset(bus->romslot1, 0x0, ROM_MAPPER_1_SIZE);
	memset(bus->ramslot2, 0x0, RAM_MAPPER_2_SIZE);
	memset(bus->systemRam, 0x0, SYSRAM_SIZE);
	memset(bus->bios, 0x0, BIOS_SIZE);

	bus->isBiosMapped = 1;
}

void memoryBusLoadBios(struct Bus* bus, const char *path)
{
	FILE* bios = fopen(path, "r");
	if (bios == NULL) {
		printf("---Bios file could not be found---\n");
		return;
	}
	fread(&bus->bios, sizeof(u8), BIOS_SIZE, bios);
	fclose(bios);
}

u8 memoryBusReadU8(struct Bus* bus, u16 address)
{
	if (bus->isBiosMapped) {
		if (address >= ROM_START && address <= (BIOS_SIZE - 1)) {
			return bus->bios[address & (BIOS_SIZE - 1)];
		}
	}
	else {
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
			return bus->systemRam[address & 0xDFFF];
		}
	}
	return 0x0;
}
