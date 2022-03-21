#include "Joypad.h"

void joypadInit(struct Joypad* joy)
{
	joy->joypad_port = 0xFF;
}

void joypadButtonPressed(struct Joypad* joy, enum Button btn, u8 pressed)
{
	if (pressed) {
		switch (btn) {
			case Up: joy->joypad_port &= ~(BIT_UP); break;
			case Down: joy->joypad_port &= ~(BIT_DOWN); break;
			case Left: joy->joypad_port &= ~(BIT_LEFT); break;
			case Right: joy->joypad_port &= ~(BIT_RIGHT); break;
			case A: joy->joypad_port &= ~(BIT_A); break;
			case B: joy->joypad_port &= ~(BIT_B); break;
		}
	}
	else {
		switch (btn) {
			case Up: joy->joypad_port ^= ~(BIT_UP); break;
			case Down: joy->joypad_port ^= ~(BIT_DOWN); break;
			case Left: joy->joypad_port ^= ~(BIT_LEFT); break;
			case Right: joy->joypad_port ^= ~(BIT_RIGHT); break;
			case A: joy->joypad_port ^= ~(BIT_A); break;
			case B: joy->joypad_port ^= ~(BIT_B); break;
		}
	}
}

u8 joypadReadPort(struct Joypad* joy)
{
	return joy->joypad_port;
}
