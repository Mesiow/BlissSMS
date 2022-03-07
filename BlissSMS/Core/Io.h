#pragma once
#include "Util.h"

struct Io {
	u8 memoryControl;
	u8 ioControl;
	u8 ioAB;
	u8 ioBMisc;

	struct Vdp* vdp;
};

void ioInit(struct Io* io);
void ioConnectVdp(struct Io* io, struct Vdp* vdp);
void ioWriteU8(struct Io *io, u8 value, u8 address);
u8 ioReadU8(struct Io* io, u8 address);
