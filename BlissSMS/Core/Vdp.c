#include "Vdp.h"
#include "Io.h"
#include "System.h"

void vdpInit(struct Vdp* vdp)
{
	memset(vdp->registers, 0xFF, 0xB);
	memset(vdp->vram, 0x0, 0x4000);
	memset(vdp->cram, 0x0, 0x20);

	//defaults name table t0 0x3800
	vdp->registers[0x2] = 0xFF;

	//must set all bits in this register, otherwise the
	//vdp will fetch pattern data and name table data incorrectly
	vdp->registers[0x3] = 0xFF;

	//bits 0 - 2 should be set otherwise vdp will
	//fetch pattern and name table data incorrectly
	vdp->registers[0x4] = 0x7;

	//defaults sprite attribute table to 0x3F00
	vdp->registers[0x5] = 0xFF;

	vdp->state = Visible;
	vdp->cycles = 0;
	vdp->mode = Mode4;
	vdp->line_int_pending = 0;
	vdp->frame_int_pending = 0;

	vdp->vdpControl = 0x0;
	vdp->vdpData = 0x0;
	vdp->hcounter = 0;
	vdp->vcounter = 0;
	vdp->lineCounter = 0xFF;

	vdp->second_control_write = 0x0;
	vdp->readbuffer = 0x0;

	vdp->display_width = DISPLAY_WIDTH;
	vdp->display_height = DISPLAY_HEIGHT;
	vdp->frame_complete = 0;

	vdp->pixels = sfImage_createFromColor(DISPLAY_WIDTH, DISPLAY_HEIGHT, sfBlack);
	if (vdp->pixels == NULL)
		printf("Image failed to create\n");

	vdp->framebuffer = sfTexture_createFromImage(vdp->pixels, NULL);
	if (vdp->framebuffer == NULL)
		printf("Texture failed to create\n");

	vdp->frame = sfSprite_create();
	sfSprite_setTexture(vdp->frame, vdp->framebuffer, sfTrue);

	sfVector2f scale = { 2, 2 };
	sfSprite_setScale(vdp->frame, scale);
}

void vdpFree(struct Vdp* vdp)
{
	sfImage_destroy(vdp->pixels);
	sfTexture_destroy(vdp->framebuffer);
	sfSprite_destroy(vdp->frame);
}

void vdpConnectIo(struct Vdp *vdp, struct Io* io)
{
	vdp->io = io;
}

void vdpUpdate(struct Vdp *vdp, u8 cycles)
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
	}

	switch (vdp->vcounter) {
		case 193: { //attempt to generate frame interrupt (Vblank period)
			vdp->lineCounter = vdp->registers[0xA]; //line counter reloaded on line 193
			vdp->frame_int_pending = 1;
			vdp->frame_complete = 1;
		}
		break;
		case 262: {
			vdp->vcounter = 0;
		}
		break;
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
	u16 line = vdp->vcounter;
	u16 nametable_base_addr = vdpGetNameTableBaseAddress(vdp);
	u8 mask_first_col = (vdp->registers[0] >> 5) & 0x1;

	s32 row = line;
	row /= 8; //a row is 8 pixels large

	//32 columns per scanline (32 x 28 tiles)
	for (s32 column = 0; column < 32; column++) {
		s32 invert = 7;
		//draw 8 pixels of the tile in this column
		for (u8 x = 0; x < 8; x++, --invert) {
			s32 xpos = x + column * 8; //set to actual pixel on screen (each column is 8 pixels)

			u16 nametable_base_offset = nametable_base_addr;
			nametable_base_offset += row * 64; //each scanline has 32 tiles (1 tile per column) but 1 tile is 2 bytes in memory
			nametable_base_offset += column * 2; //each tile is two bytes in memory

			//Get 2 byte tile data
			u16 tile_data = vdp->vram[nametable_base_offset + 1] << 8;
			tile_data |= vdp->vram[nametable_base_offset];

			u8 priority = (tile_data >> 12) & 0x1;
			u8 palette_select = (tile_data >> 11) & 0x1;
			u8 vertical_flip = (tile_data >> 10) & 0x1;
			u8 horizontal_flip = (tile_data >> 9) & 0x1;
			u16 pattern_index = (tile_data & 0x1FF);

			s32 offset = line; //offset will point to which line of pattern to draw

			offset %= 8; //tile is 8 pixels so wrap if gone past

			//each pattern is 32 bytes in memory
			pattern_index *= 32;
			//each pattern line is 4 bytes, offset contains correct pattern line
			pattern_index += 4 * offset;

			//get pattern line data
			u8 d1 = vdp->vram[pattern_index];
			u8 d2 = vdp->vram[pattern_index + 1];
			u8 d3 = vdp->vram[pattern_index + 2];
			u8 d4 = vdp->vram[pattern_index + 3];

			s32 color_bit = invert; //color is read left to right
			//if(horizontal_flip)
				//color_bit = x; //color read from right to left

			//Get which palette for this pattern line
			u8 palette = 0;
			u8 bit = testBit(d4, color_bit);
			palette = (bit << 3);
			bit = testBit(d3, color_bit);
			palette |= (bit << 2);
			bit = testBit(d2, color_bit);
			palette |= (bit << 1);
			bit = testBit(d1, color_bit);
			palette |= bit;

			//a tile can only have high priority if it isnt palette 0
			if (palette == 0) priority = 0;
			if (palette_select) palette += 16; //sprite palette is used

			u8 color = vdp->cram[palette];

			u8 red = color & 0x3;
			u8 green = (color >> 2) & 0x3;
			u8 blue = (color >> 4) & 0x3;

			if (xpos >= vdp->display_width)
				continue;
			
			sfImage_setPixel(vdp->pixels, xpos, line, vdpGetColor(red, green, blue));
		}
	}
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

u8 vdpFrameComplete(struct Vdp* vdp)
{
	if (vdp->frame_complete) {
		vdp->frame_complete = 0;
		return 1;
	}
	return 0;
}

void vdpWriteControlPort(struct Vdp* vdp, u8 value)
{
	//printf("control port write\n");
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
				/*vram read*/
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

				if (register_number < 0xB) {
					vdp->registers[register_number] = data;
				}

				vdp->writes_to_vram = 1;
			} 
			break;
			case 3: vdp->writes_to_vram = 0; break;

			default: 
				printf("invalid code register value\n"); 
				break;
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
	//printf("vdp data write\n");
	if (vdp->writes_to_vram) {
		//printf("vram write\n");
		u16 address_reg = vdpGetAddressRegister(vdp);
		vdp->vram[address_reg] = value;

		vdpIncrementAddressRegister(vdp);
	}
	else { //write to cram
		//printf("cram write\n");
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
	vdp->status_flags = 0x1F;
	vdp->status_flags |= (vdp->frame_int_pending << 7);
	//todo: store sprite overflow and collision as well

	//reset
	vdp->line_int_pending = 0;
	vdp->frame_int_pending = 0;

	return vdp->status_flags;
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

u16 vdpGetNameTableBaseAddress(struct Vdp* vdp)
{
	u8 name_table_addr = vdp->registers[0x2];
	if (vdp->display_height == DISPLAY_HEIGHT) {
		//create word using only bits 1 - 3 of register 2
		name_table_addr &= 0xF;
		name_table_addr = clearBit(name_table_addr, 0);
		
		u16 base_addr = ((u16)name_table_addr) << 10;
		return base_addr;
	}

	//Todo: handle other display resolutions
}

u16 vdpGetSpriteAttributeTableBaseAddress(struct Vdp* vdp)
{
	u8 sprite_table_addr = vdp->registers[0x5];
	
	//bit 0 and 7 ignored
	sprite_table_addr &= 0x7E;
	u16 base_addr = ((u16)sprite_table_addr) << 7;

	return base_addr;
}

sfColor vdpGetColor(u8 red, u8 green, u8 blue)
{
	sfColor colr;
	colr.r = vdpGetColorShade(red);
	colr.g = vdpGetColorShade(green);
	colr.b = vdpGetColorShade(blue);
	colr.a = 255;

	return colr;
}

u8 vdpGetColorShade(u8 color)
{
	switch (color) {
		case 0: return 0;
		case 1: return 85;
		case 2: return 170; //midpoint between 0 and 255
		case 3: return 255;
		default: printf("--color shade does not exist--\n"); assert(0); break;
	}
}

u8 vdpPendingInterrupts(struct Vdp* vdp)
{
	//Check for vblank interrupt
	if (vdp->frame_int_pending && vdpVBlankIrqReady(vdp)) return 1;
	//Check for line interrupt
	if (vdp->line_int_pending && vdpLineInterruptReady(vdp)) return 1;

	return 0;
}

u8 vdpVBlankIrqReady(struct Vdp* vdp)
{
	return testBit(vdp->registers[1], 5);
}

u8 vdpLineInterruptReady(struct Vdp* vdp)
{
	return testBit(vdp->registers[0], 4);
}
