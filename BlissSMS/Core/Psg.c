#include "Psg.h"

void psgInit(struct Psg* psg)
{
	float two_decibels = 0.8f; //each volume in the table up to 0xF is 2 decibels quieter than the previous
	s32 max_volume = 8000;

	float curr_volume = max_volume;
	for (s32 i = 0; i < 0x10; i++) {
		psg->volume_table[i] = curr_volume;
		curr_volume *= two_decibels; //next vol is lower by 2 decibels
	}
	psg->volume_table[0xF] = 0; //0b1111 is off
}

void psgUpdate(struct Psg* psg, u8 cycles)
{
	
}

void psgWritePort(struct Psg* psg, u8 value)
{
	psg->latched_channel = ((value >> 5) & 0x3);
	psg->latched_type = ((value >> 4) & 0x1);

	//Latch register write
	if ((value >> 7) & 0x1) {
		//Set channels volume
		if (psg->latched_type) {
			u8 vol = value & 0xF;
			psg->volume[psg->latched_channel] = vol;
		}
		//Tone/noise
		else {
			switch (psg->latched_channel) {
			case 0: case 1: case 2: //pulse tones
				psg->tone[psg->latched_channel] &= ~0xF; //clear lower nibble (frequency)
				psg->tone[psg->latched_channel] |= (value & 0xF);
				break;

			case 3: break; //noise channel
			}
		}
	}
	//Second write to set frequency
	else {
		u8 data = value & 0x3F; //total data is 6 bits
		if (psg->latched_type) {
			u8 vol = data & 0xF;
			psg->volume[psg->latched_channel] = vol;
		}
		else {
			switch (psg->latched_channel) {
			case 0: case 1: case 2:
				psg->tone[psg->latched_channel] &= 0xF;
				psg->tone[psg->latched_channel] |= (data << 4); //hhhhhh is upper 6 bit value of the 10 bit value
				break;

			case 3: break; //noise channel
			}
		}
	}
}
