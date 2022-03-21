#pragma once
#include "Util.h"

struct Io {
	struct Vdp* vdp;
	struct Bus* bus;
};

void ioInit(struct Io* io);
void ioConnectVdp(struct Io* io, struct Vdp* vdp);
void ioConnectBus(struct Io* io, struct Bus* bus);
void ioWriteU8(struct Io *io, u8 value, u8 address);
u8 ioReadU8(struct Io* io, u8 address);
