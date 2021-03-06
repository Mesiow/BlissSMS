#pragma once
#include "Util.h"

struct Io {
	u8 nationalization_port;

	struct Vdp* vdp;
	struct Psg* psg;
	struct Joypad* joy;
	struct Bus* bus;
};

void ioInit(struct Io* io);
void ioConnectVdp(struct Io* io, struct Vdp* vdp);
void ioConnectPsg(struct Io* io, struct Psg* psg);
void ioConnectJoypad(struct Io* io, struct Joypad* joy);
void ioConnectBus(struct Io* io, struct Bus* bus);
void ioWriteU8(struct Io *io, u8 value, u8 address);
u8 ioReadU8(struct Io* io, u8 address);

void ioResetButtonPressed(struct Io* io, u8 pressed);
