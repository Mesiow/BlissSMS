#include "Joypad.h"

void joypadInit(struct Joypad* joy)
{
	joy->joypad_port = 0xFF;
	joy->joypad_port2 = 0xFF;
	joy->joypad_temp = 0xFF;
}

void joypadUpdate(struct Joypad* joy)
{
	//update the joypad port
	joy->joypad_port = joy->joypad_temp;
}

void joypadButtonPressed(struct Joypad* joy, enum Button btn, u8 pressed)
{
	if (pressed) {
		switch (btn) {
			case Up: joy->joypad_temp &= ~(BIT_UP); break;
			case Down: joy->joypad_temp &= ~(BIT_DOWN); break;
			case Left: joy->joypad_temp &= ~(BIT_LEFT); break;
			case Right: joy->joypad_temp &= ~(BIT_RIGHT); break;
			case A: joy->joypad_temp &= ~(BIT_A); break;
			case B: joy->joypad_temp &= ~(BIT_B); break;
		}
	}
	else {
		switch (btn) {
			case Up: joy->joypad_temp ^= (BIT_UP); break;
			case Down: joy->joypad_temp ^= (BIT_DOWN); break;
			case Left: joy->joypad_temp ^= (BIT_LEFT); break;
			case Right: joy->joypad_temp ^= (BIT_RIGHT); break;
			case A: joy->joypad_temp ^= (BIT_A); break;
			case B: joy->joypad_temp ^= (BIT_B); break;
		}
	}
}
