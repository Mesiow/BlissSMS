#include "Vdp.h"

void vdpInit(struct Vdp* vdp)
{
	memset(vdp->tilePatterns0, 0x0, 0x2000);
	memset(vdp->tilePatterns1, 0x0, 0x1800);
	memset(vdp->screenDisplay, 0x0, 0x700);
	memset(vdp->spriteInfo, 0x0, 0x100);
}

void vdpConnectIo(struct Vdp *vdp, struct Io* io)
{
	vdp->io = io;
}
