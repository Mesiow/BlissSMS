#include "Vdp.h"

void vdpInit(struct Vdp* vdp)
{
	memset(vdp->tilePatterns0, 0x0, 0x2000);
	memset(vdp->tilePatterns1, 0x0, 0x1800);
	memset(vdp->screenDisplay, 0x0, 0x700);
	memset(vdp->spriteInfo, 0x0, 0x100);

	vdp->state = Visible;
	vdp->cycles = 0;
}

void vdpConnectIo(struct Vdp *vdp, struct Io* io)
{
	vdp->io = io;
}

void vdpUpdate(struct Vdp *vdp, s32 cycles)
{
	vdp->cycles += cycles;
	switch (vdp->state) {
		case Visible: {

		}
		break;
		case HBlank: {

		}
		break;
		case VBlank: {

		}
		break;
	}
}

u8 vdpPendingInterrupts(struct Vdp* vdp)
{
	
}
