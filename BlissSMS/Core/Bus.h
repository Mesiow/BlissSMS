#pragma once
#include "Util.h"

/*
	Master System/Mark III (assuming Sega mapper)
	Region	Maps to
	$0000-$03ff	ROM (unpaged)
	$0400-$3fff	ROM mapper slot 0
	$4000-$7fff	ROM mapper slot 1
	$8000-$bfff	ROM/RAM mapper slot 2
	$c000-$dfff	System RAM
	$e000-$ffff	System RAM (mirror)
	$fff8	3D glasses control
	$fff9-$fffb	3D glasses control (mirrors)
	$fffc	Cartridge RAM mapper control
	$fffd	Mapper slot 0 control
	$fffe	Mapper slot 1 control
	$ffff	Mapper slot 2 control
*/

#define ROM_SIZE 0x400 
#define ROM_MAPPER_0_SIZE 0x3C00
#define ROM_MAPPER_1_SIZE 0x4000
#define RAM_MAPPER_2_SIZE 0x4000
#define SYSRAM_SIZE 0x2000
#define BIOS_SIZE 0x2000

#define CART_RAM_MAPPER_CTRL 0xFFFC
#define MAPPER_SLOT_0_CTRL 0xFFFD
#define MAPPER_SLOT_1_CTRL 0xFFFE
#define MAPPER_SLOT_2_CTRL 0xFFFF

#define ROM_START 0x0000
#define ROM_END 0x3FF
#define ROM_SLOT_0_START 0x400
#define ROM_SLOT_0_END 0x3FFF
#define ROM_SLOT_1_START 0x4000
#define ROM_SLOT_1_END 0x7FFF

#define RAM_SLOT_2_START 0x8000
#define RAM_SLOT_2_END 0xBFFF
#define SYSRAM_START 0xC000
#define SYSRAM_END 0xDFFF

//Mapping Registers
#define RAM_MAPPING_REGISTER 0xFFFC
#define ROM_MAPPING_0 0xFFFD
#define ROM_MAPPING_1 0xFFFE
#define ROM_MAPPING_2 0xFFFF

struct Bus {
	u8 rom[ROM_SIZE];
	u8 romslot0[ROM_MAPPER_0_SIZE];
	u8 romslot1[ROM_MAPPER_1_SIZE];
	u8 ramslot2[RAM_MAPPER_2_SIZE];
	u8 systemRam[SYSRAM_SIZE];
	u8 bios[SYSRAM_SIZE];

	//Memory control bits
	u8 cart_slot_enabled;
	u8 wram_enabled;
	u8 bios_enabled;
	u8 io_enabled;

	struct Cart* cart;
};

void memoryBusInit(struct Bus* bus);
void memoryBusLoadBios(struct Bus* bus, const char *path);
void memoryBusLoadCartridge(struct Bus* bus, struct Cart *cart);

void memoryBusWriteU8(struct Bus* bus, u8 value, u16 address);
void writeMemoryControl(struct Bus* bus, u8 value);
u8 memoryBusReadU8(struct Bus* bus, u16 address);

u8 memoryBusReadRamMapper(struct Bus* bus);
u8 memoryBusReadRomBankRegister(struct Bus* bus, u16 rombank);