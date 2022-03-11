#include "Vdp.h"
#include "Io.h"
#include "System.h"

void vdpInit(struct Vdp* vdp)
{
	memset(vdp->registers, 0xFF, 0xB);
	memset(vdp->vram, 0x0, 0x4000);
	memset(vdp->cram, 0x0, 0x20);

	vdp->state = Visible;
	vdp->cycles = 0;
	vdp->mode = Mode4;
	vdp->line_int_pending = 0;

	vdp->vdpControl = 0x0;
	vdp->vdpData = 0x0;
	vdp->hcounter = 0;
	vdp->vcounter = 0;
	vdp->lineCounter = 0;

	vdp->second_control_write = 0x0;
	vdp->readbuffer = 0x0;

	vdp->pixels = sfImage_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
	vdp->pixels = sfImage_createFromColor(DISPLAY_WIDTH, DISPLAY_HEIGHT, sfWhite);

	vdp->framebuffer = sfTexture_createFromImage(vdp->pixels, NULL);
	vdp->frame = sfSprite_create();
	sfSprite_setTexture(vdp->frame, vdp->framebuffer, sfTrue);

	sfVector2f scale = { 2, 2 };
	sfSprite_setScale(vdp->frame, scale);
}

void vdpConnectIo(struct Vdp *vdp, struct Io* io)
{
	vdp->io = io;
}

void vdpUpdate(struct Vdp *vdp, s32 cycles)
{
	vdp->cycles += cycles;
	if (vdp->cycles >= CYCLES_PER_SCANLINE) {
		if (vdp->vcounter <= 192) {
			vdp->lineCounter--;

			//underflow from 0 to 0xFF sets irq and reloads line counter
			if (vdp->lineCounter == 0xFF) {
				vdp->lineCounter = vdp->registers[0xA];
				vdp->line_int_pending = 1;
			}

			//Render at start of new line
			if (vdpIsDisplayVisible(vdp)) {
				if (vdpIsDisplayActive(vdp)) {
					vdpRender(vdp);
				}
			}
		}

		vdp->cycles -= CYCLES_PER_SCANLINE;
		vdp->vcounter++;

		switch (vdp->vcounter) {
			case 193: { //attempt to generate frame interrupt (Vblank period)
				vdp->status_flags = setBit(vdp->status_flags, 7); //set frame int pending
				vdp->lineCounter = vdp->registers[0xA];
			}
			break;
			case 262: {
				vdp->vcounter = 0;
			}
			break;
		}
	}
}

void vdpDisplayGraphics(struct Vdp* vdp, sfRenderWindow *window)
{
	sfRenderWindow_drawSprite(window, vdp->frame, NULL);
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

void vdpBufferPixels(struct Vdp* vdp)
{
	sfTexture_updateFromImage(vdp->framebuffer, vdp->pixels, 0, 0);
}

u8 vdpIsDisplayVisible(struct Vdp* vdp)
{
	return (vdp->registers[1] >> 6) & 0x1;
}

u8 vdpIsDisplayActive(struct Vdp* vdp)
{
	return vdp->vcounter < 192;
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
		u8 code_reg = vdpGetCodeRegister(vdp);
		switch (code_reg) {
			case 0: {
				u16 address_reg = vdpGetAddressRegister(vdp);
				vdp->readbuffer = vdp->vram[address_reg];

				vdpIncrementAddressRegister(vdp);
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
	vdp->second_control_write = 0;

	if (vdp->writes_to_vram) {
		u16 address_reg = vdpGetAddressRegister(vdp);
		vdp->vram[address_reg] = value;

		vdpIncrementAddressRegister(vdp);
	}
	else { //write to cram
		u16 address_reg = vdpGetAddressRegister(vdp);
		//cram is only 32 bytes
		address_reg &= 0x1F;
		vdp->cram[address_reg] = value;

		vdpIncrementAddressRegister(vdp);
	}

	vdp->readbuffer = value;
}

u8 vdpReadControlPort(struct Vdp* vdp)
{
	vdp->second_control_write = 0;
	vdp->line_int_pending = 0;

	u8 status_flags = vdp->status_flags;
	vdp->status_flags = 0;

	return status_flags;
}

u8 vdpReadDataPort(struct Vdp* vdp)
{
	vdp->second_control_write = 0;

	u16 address_reg = vdpGetAddressRegister(vdp);
	u8 buffer = vdp->readbuffer;

	//vram is buffered on every data port read regardless of the code register
	//and the contents of the buffer before the buffer update is returned
	vdp->readbuffer = vdp->vram[address_reg];

	vdpIncrementAddressRegister(vdp);

	return buffer;
}

void vdpIncrementAddressRegister(struct Vdp* vdp)
{
	//increment address register
	if (vdpGetAddressRegister(vdp) == 0x3FFF) //overflow to 0
		vdp->vdpControl &= 0xC000; //keep code register unchanged
	else
		vdp->vdpControl++;
}

u8 vdpGetCodeRegister(struct Vdp* vdp)
{
	u8 code_register = (vdp->vdpControl >> 14) & 0x3;
	return code_register;
}

u16 vdpGetAddressRegister(struct Vdp* vdp)
{
	u16 address_register = (vdp->vdpControl) & 0x3FFF;
	return address_register;
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
