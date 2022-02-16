#pragma once
#include "Util.h"

struct Io {
	u8 memoryControl;
	u8 ioControl;
	u8 vcounter;
	u8 hcounter;
	u8 vdpData;
	u8 vdpControl;
	u8 ioAB;
	u8 ioBMisc;
};

void ioInit(struct Io* io);
void ioWriteU8(struct Io *io, u8 value, u8 address);
