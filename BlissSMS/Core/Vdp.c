#include "Vdp.h"
#include "Io.h"

void vdpInit(struct Vdp* vdp)
{
	memset(vdp->registers, 0x0, 0xA);
	memset(vdp->vram, 0x0, 0x4000);
	memset(vdp->cram, 0x0, 0x20);

	vdp->state = Visible;
	vdp->cycles = 0;
	vdp->frame_int_enable = 0;
	vdp->mode = Mode4;

	vdp->vdpControl = 0x0;
	vdp->vdpData = 0x0;
	vdp->second_control_write = 0x0;
	vdp->readbuffer = 0x0;
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

void vdpRender(struct Vdp* vdp)
{
	vdpSetMode(vdp);
	if (vdp->mode == Mode4) {
		vdpRenderBackground(vdp);
		vdpRenderSprites(vdp);
	}
}

void vdpRenderBackground(struct Vdp* vdp)
{

}

void vdpRenderSprites(struct Vdp* vdp)
{
}

void vdpSetMode(struct Vdp* vdp)
{
	u8 mode = (vdp->registers[0] >> 2) & 0x1;
	if (mode == 0x1) {
		vdp->mode = Mode4;
	}
}

void vdpWriteControlPort(struct Vdp* vdp, u8 value)
{
	//Command word write
	if (vdp->second_control_write) {
		//Updates most significant byte
		vdp->second_control_write = 0;
		vdp->vdpControl &= 0xFF;
		vdp->vdpControl |= (value << 8);

		//Code register
		u8 code_reg = (vdp->vdpControl >> 14) & 0x3;
		switch (code_reg) {
			case 0: {
				u16 address_reg = (vdp->vdpControl) & 0x3FFF;
				vdp->readbuffer = vdp->vram[address_reg];

				//increment address register
				if (address_reg == 0x3FFF) //overflow to 0
					vdp->vdpControl &= 0xC000; //keep code register unchanged
				else
					vdp->vdpControl++;

			}break;
			case 1: vdp->writes_to_vram = 1; break;
			case 2: /*vdp register write*/ break;
			case 3: vdp->writes_to_vram = 0; break;
		}
	}
	else {
		//Updates least significant byte
		vdp->second_control_write = 1;
		vdp->vdpControl &= 0xFF00;
		vdp->vdpControl |= value;
	}
}

void vdpWriteDataPort(struct Vdp* vdp, u8 value)
{

}

u8 vdpReadControlPort(struct Vdp* vdp)
{
	return 0;
}

u8 vdpReadDataPort(struct Vdp* vdp)
{
	return 0;
}

u8 vdpPendingInterrupts(struct Vdp* vdp)
{
	//Check for vblank interrupt
	vdp->frame_int_enable = testBit(vdp->registers[1], 5);
	if (vdp->frame_int_enable && vdpFrameInterruptPending(vdp))
		return 1;

	//Check for line interrupt
	if (vdpLineInterruptEnable(vdp) && vdp->io->hcounter < 0)
		return 1;

	return 0;
}

u8 vdpFrameInterruptPending(struct Vdp* vdp)
{
	u8 frame_int = testBit(vdp->vdpControl, 7);
	return frame_int;
}

u8 vdpLineInterruptEnable(struct Vdp* vdp)
{
	u8 line_int = testBit(vdp->registers[0], 4);
	return line_int;
}
