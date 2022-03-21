#include "Joypad.h"

void joypadInit(struct Joypad* joy)
{
	joy->joypad_port = 0xFF;
}

void joypadButtonPressed(enum Button btn, u8 pressed)
{
	if (pressed) {

	}
	else {

	}
}

u8 joypadReadPort(struct Joypad* joy)
{
	return joy->joypad_port;
}
