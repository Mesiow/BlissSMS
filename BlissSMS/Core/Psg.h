#pragma once
#include "Util.h"
#include "SFML\Audio.h"

struct Psg {
	u16 cycles;

	u8 latched_type;
	u8 latched_channel;

	u16 tones[4]; //tones for each channel
	u8 volume[4]; //volume for each channel
	u16 volume_table[0x10]; //volume lookup for the 4 channels (3 pulse, 1 noise)
};

void psgInit(struct Psg* psg);
void psgUpdate(struct Psg *psg, u8 cycles);
void psgWritePort(struct Psg* psg, u8 value);