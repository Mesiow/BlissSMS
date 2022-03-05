#include "Vdp.h"
#include "Io.h"

void vdpInit(struct Vdp* vdp)
{
	memset(vdp->registers, 0x0, 0xA);
	memset(vdp->vram, 0x0, 0x4000);
	memset(vdp->cram, 0x0, 0x20);

	vdp->state = Visible;
	vdp->cycles = 0;
	vdp->mode = Mode4;
	vdp->line_int_pending = 0;

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

				vdp->writes_to_vram = 1;

			}
			break;
			case 1: vdp->writes_to_vram = 1; break;
			case 2: { 
				/*vdp register write*/

				//First byte written holds the data
				//Second byte written holds the register index/number
				u8 data = vdp->vdpControl & 0xFF;
				u8 register_number = value & 0xF;

				if(register_number < 0xB)
					vdp->registers[register_number] = data;

				vdp->writes_to_vram = 1;
			} 
			break;
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
	u8 status_flags = vdp->status_flags;
	for (s32 i = 5; i <= 7; i++) vdp->status_flags = clearBit(vdp->status_flags, i);

	return status_flags;
}

u8 vdpReadDataPort(struct Vdp* vdp)
{
	return 0;
}

u8 vdpPendingInterrupts(struct Vdp* vdp)
{
	//Check for vblank interrupt
	if (vdpCheckFrameInterruptEnable(vdp) && vdpVBlankIrqPending(vdp))
		return 1;

	//Check for line interrupt
	if (vdpLineInterruptEnable(vdp) && vdpLineInterruptPending(vdp))
		return 1;

	return 0;
}

u8 vdpCheckFrameInterruptEnable(struct Vdp* vdp)
{
	u8 frame_int_enable = testBit(vdp->registers[1], 5);
	return frame_int_enable;
}

u8 vdpVBlankIrqPending(struct Vdp* vdp)
{
	u8 frame_int_pending = testBit(vdp->status_flags, 7);
	return frame_int_pending;
}

u8 vdpLineInterruptEnable(struct Vdp* vdp)
{
	u8 line_int_enable = testBit(vdp->registers[0], 4);
	return line_int_enable;
}

u8 vdpLineInterruptPending(struct Vdp* vdp)
{
	return vdp->line_int_pending;
}
