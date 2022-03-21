#pragma once
#include "Util.h"

enum Button {
	Up = 0,
	Down,
	Left,
	Right,
	A,
	B
};

struct Joypad {
	u8 joypad_port;
};

void joypadInit(struct Joypad* joy);
void joypadButtonPressed(enum Button btn, u8 pressed);
u8 joypadReadPort(struct Joypad* joy);