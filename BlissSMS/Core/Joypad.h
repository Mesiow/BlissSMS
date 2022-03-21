#pragma once
#include "Util.h"
#include "SFML\Window.h"

#define BIT_UP (1 << 0)
#define BIT_DOWN (1 << 1)
#define BIT_LEFT (1 << 2)
#define BIT_RIGHT (1 << 3)
#define BIT_A (1 << 4)
#define BIT_B (1 << 5)

enum Button {
	Up = sfKeyUp,
	Down = sfKeyDown,
	Left = sfKeyLeft,
	Right = sfKeyRight,
	A = sfKeyA,
	B = sfKeyS
};

struct Joypad {
	u8 joypad_port;
};

void joypadInit(struct Joypad* joy);
void joypadButtonPressed(struct Joypad *joy, enum Button btn, u8 pressed);
u8 joypadReadPort(struct Joypad* joy);