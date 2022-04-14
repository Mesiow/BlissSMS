#include "Vdp.h"
#include "Io.h"
#include "System.h"

void vdpInit(struct Vdp* vdp)
{
	memset(vdp->registers, 0xFF, 0xB);
	memset(vdp->vram, 0x0, 0x4000);
	memset(vdp->cram, 0x0, 0x20);

	vdp->registers[0x0] = 0x36;
	vdp->registers[0x1] = 0x80;
	//defaults name table t0 0x3800
	vdp->registers[0x2] = 0xFF;

	//must set all bits in this register, otherwise the
	//vdp will fetch pattern data and name table data incorrectly
	vdp->registers[0x3] = 0xFF;

	//bits 0 - 2 should be set otherwise vdp will
	//fetch pattern and name table data incorrectly
	vdp->registers[0x4] = 0xFF;

	//defaults sprite attribute table to 0x3F00
	vdp->registers[0x5] = 0xFF;
	//unused bits should be set to 1
	vdp->registers[0x6] = 0xFB;
	vdp->registers[0xA] = 0xFF;

	vdp->state = Visible;
	vdp->cycles = 0;
	vdp->mode = Mode4;
	vdp->line_int_pending = 0;
	vdp->frame_int_pending = 0;

	vdp->sprite_overflow = 0;
	vdp->sprite_collision = 0;
	vdp->y_scroll = 0;
	
	memset(vdp->priority_buffer, 0x0, DISPLAY_WIDTH);

	vdp->vdp_control = 0x0;
	vdp->vdp_data = 0x0;
	vdp->hcounter = 0;
	vdp->vcounter = 0;
	vdp->vcount_port = 0;
	vdp->line_counter = 0xFF;

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
			vdp->line_counter--;

			//underflow from 0 to 0xFF sets line irq and reloads line counter
			if (vdp->line_counter == 0xFF) {
				vdp->line_counter = vdp->registers[0xA];
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
		vdp->vcount_port++;

		vdpScanlineUpdate(vdp);
	}
}

void vdpScanlineUpdate(struct Vdp* vdp)
{
	//reload line counter between lines 193 - 261
	if(vdp->vcounter >= 193 && vdp->vcounter <= 261)
		vdp->line_counter = vdp->registers[0xA];

	switch (vdp->vcounter) {
		case 193: vdp->y_scroll = vdp->registers[0x9]; break; //y scroll is only updated once the display enters vblank
		//attempt to generate frame interrupt (Vblank period)
		case 196: vdp->frame_int_pending = 1; break;
		case 218: vdp->vcount_port = 213; break;
		case 262: {
			vdp->frame_complete = 1;
			vdp->vcounter = 0;
			vdp->vcount_port = 0;
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

	//Horizontal scroll
	u8 x_scroll = vdp->registers[0x8];
	u8 x_start_col = (x_scroll >> 3) & 0x1F;
	u8 x_fine_scroll = x_scroll & 0x7;

	u8 disable_xscroll = (vdp->registers[0] >> 6) & 0x1; //disable xscrolling for rows 0 - 1

	//Vertical scroll
	u8 y_scroll = vdp->y_scroll;
	u8 y_start_row = (y_scroll >> 3) & 0x1F;
	u8 y_fine_scroll = y_scroll & 0x7;

	u8 disable_yscroll = (vdp->registers[0] >> 7) & 0x1;
	
	//32 columns per scanline (32 x 28 tiles)
	for (s32 column = 0; column < 32; column++) {
		//draw 8 pixels of the tile in this column
		for (u8 x = 0; x < 8; x++) {
			s32 xpixel_pos = x + (column * 8);

			u8 use_xscroll = 0;
			if ((row > 1) || !disable_xscroll)
				use_xscroll = 1;
			
			if (use_xscroll) {
				xpixel_pos = x_start_col; //horizontal start column
				xpixel_pos *= 8; //each col is 8 pixels
				xpixel_pos += ((x + x_fine_scroll)); //add current pixel and fine scroll value to the position
				xpixel_pos = xpixel_pos % DISPLAY_WIDTH; //wrap with display width if position exceeds it
			}

			//Vertical scrolling
			u8 use_yscroll = 1;
			//if col is less than 23 allow y scrolling (y scrolling disabled if drawing columns 24 - 32)
			//and if disable y scroll is not set
			if (disable_yscroll && column >= 24)
				use_yscroll = 0;

			u16 vertical_offset = row;
			if (use_yscroll) {
				vertical_offset += y_start_row; //add starting row value
				s32 add_row = line % 8; //add fine scroll value (might move us to next row)
				if ((add_row + y_fine_scroll) > 7)
					vertical_offset++;

				//wrap
				s32 num_rows = 28;
				vertical_offset %= num_rows;
			}
	
			//Tile map data
			u16 nametable_base_offset = nametable_base_addr;
			nametable_base_offset += vertical_offset * (32 * 2); //each scanline has 32 tiles (1 tile per column) but 1 tile is 2 bytes in memory
			nametable_base_offset += column * 2; //each tile is two bytes in memory

			//Get 2 byte tile data
			u16 tile_data = vdp->vram[nametable_base_offset + 1] << 8;
			tile_data |= vdp->vram[nametable_base_offset];

			u8 priority = (tile_data >> 12) & 0x1;
			u8 palette_select = (tile_data >> 11) & 0x1;
			u8 vertical_flip = (tile_data >> 10) & 0x1;
			u8 horizontal_flip = (tile_data >> 9) & 0x1;
			u16 pattern_index = (tile_data & 0x1FF); //pattern index for tile to retrieve

			s32 offset = line; //offset will point to which line of pattern to draw
			
			if (use_yscroll)
				offset += y_scroll;

			offset %= 8; //tile is 8 pixels so wrap if gone past

			if (vertical_flip) {
				//invert offset
				offset *= -1;
				offset += 7;
			}

			//each pattern is 32 bytes in memory
			pattern_index *= 32;
			//each pattern line is 4 bytes, offset contains correct pattern line
			pattern_index += 4 * offset;

			//get pattern line data
			u8 d1 = vdp->vram[pattern_index];
			u8 d2 = vdp->vram[pattern_index + 1];
			u8 d3 = vdp->vram[pattern_index + 2];
			u8 d4 = vdp->vram[pattern_index + 3];

			u8 color_bit = 7 - x; //color is read left to right
			if(horizontal_flip)
				color_bit = x; //color read from right to left

			//Get which palette for this pattern line
			u8 palette = testBit(d4, color_bit) << 3;
			palette |= testBit(d3, color_bit) << 2;
			palette |= testBit(d2, color_bit) << 1;
			palette |= testBit(d1, color_bit);

			//a tile can only have high priority if it isnt palette 0
			if (palette == 0) priority = 0;
			if (palette_select) palette += 16; //sprite palette is used

			u8 color = 0;
			if (mask_first_col && (xpixel_pos < 8)) {
				u8 overscan_bgdrop_color = vdp->registers[0x7] & 0xF;
				color = vdp->cram[overscan_bgdrop_color + 16]; //color is from sprite palette
			}
			else
				color = vdp->cram[palette];

			u8 red = color & 0x3;
			u8 green = (color >> 2) & 0x3;
			u8 blue = (color >> 4) & 0x3;

			if (xpixel_pos >= vdp->display_width)
				continue;

			//sprite is drawn under tile if priority is set and pal isnt 0
			if (priority && palette != 0) vdp->priority_buffer[xpixel_pos] = 1;
			else vdp->priority_buffer[xpixel_pos] = 0;
			
			sfImage_setPixel(vdp->pixels, xpixel_pos, line, vdpGetColor(red, green, blue));
		}
		x_start_col++;
		x_start_col %= 32; //move onto next column and keep within 32 col range
	}
}

void vdpRenderSprites(struct Vdp* vdp)
{
	u16 line = vdp->vcounter;
	u16 sat_base_addr = vdpGetSpriteAttributeTableBaseAddress(vdp);

	u8 shift_sprites_left = (vdp->registers[0] >> 3) & 0x1;
	u8 is_sprite_8x16 = (vdp->registers[1] >> 1) & 0x1; //0 = 8x8, 1 = 8x16
	u8 is_sprite_doubled = (vdp->registers[1] & 0x1);
	u8 use_second_pattern = (vdp->registers[6] >> 2) & 0x1;
	u8 sprite_count = 0;
	u8 sprites_drawn[DISPLAY_WIDTH] = { 0 };

	u8 sprite_size = 8;
	if (is_sprite_8x16 || is_sprite_doubled)
		sprite_size = 16;

	for (s32 sprite = 0; sprite < 64; sprite++) { //max of 64 sprites
		//get y position of sprite
		s16 y = vdp->vram[sat_base_addr + sprite];
							  //when sprite y is 0xD0
		if (y == 0xD0) break; //sprites not drawn in 192 line display mode if y == 0xD0
		
		if (y > 192) y -= 0x100; //wrap when top part of sprite is off screen
		y++;//y position is always y + 1

		//Is sprite in range of visible display
		if ((line >= y) && (line < (y + sprite_size))) {
			sprite_count++;
			if (sprite_count > 8) { //only 8 sprites per line allowed, set overflow
				vdp->sprite_overflow = 1;
				break;
			}

			s32 offset_to_x_coord = 128 + (sprite * 2);
			s32 sprite_x = vdp->vram[sat_base_addr + offset_to_x_coord];
			u16 pattern_index = vdp->vram[sat_base_addr + 1 + offset_to_x_coord];

			if (shift_sprites_left) sprite_x -= 8;
			if (use_second_pattern) pattern_index += 256;

			if (is_sprite_8x16) {
				//bit 0 of pattern index ignored
				if(y < (line + 9))
					pattern_index &= ~0x1;
			}

			//each sprite tile takes 32 bytes in memory
			//(4 bytes per line)
			pattern_index *= 32;

			//get mem location for current line being drawn
			//of tile, each line is 4 bytes
			pattern_index += (4 * (line - y));
			

			//get pattern line data
			u8 d1 = vdp->vram[pattern_index];
			u8 d2 = vdp->vram[pattern_index + 1];
			u8 d3 = vdp->vram[pattern_index + 2];
			u8 d4 = vdp->vram[pattern_index + 3];

			//render 8 pixels for current tile line
			for (s32 i = 0; i < 8; i++) {
				s32 x_idx = i + sprite_x;

				if (x_idx >= DISPLAY_WIDTH) break; //skip if off screen
				if (x_idx < 8) continue; //skip if column 0
				if (vdp->priority_buffer[x_idx]) continue; //dont draw if bg has priority over sprite

				//Get which palette for this pattern line
				u8 palette = testBit(d4, 7 - i) << 3;
				palette |= testBit(d3, 7 - i) << 2;
				palette |= testBit(d2, 7 - i) << 1;
				palette |= testBit(d1, 7 - i);

				//transparent
				if (palette == 0) continue;

				if (sprites_drawn[x_idx]) {
					vdp->sprite_collision = 1;
					continue;
				}

				sprites_drawn[x_idx] = 1;

				u8 color = vdp->cram[palette + 16]; //sprites use second palette area

				u8 red = color & 0x3;
				u8 green = (color >> 2) & 0x3;
				u8 blue = (color >> 4) & 0x3;

				sfImage_setPixel(vdp->pixels, x_idx, line, vdpGetColor(red, green, blue));
			}
		}
	}
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
	//Command word write
	if (vdp->second_control_write) {
		//Updates most significant byte
		vdp->second_control_write = 0;
		vdp->vdp_control &= 0xFF;
		vdp->vdp_control |= (value << 8);

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
				u8 data = vdp->vdp_control & 0xFF;
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
		vdp->vdp_control &= 0xFF00;
		vdp->vdp_control |= value;
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
	vdp->status_flags = 0x1F;
	vdp->status_flags |= (vdp->frame_int_pending << 7);
	vdp->status_flags |= (vdp->sprite_overflow << 6);
	vdp->status_flags |= (vdp->sprite_collision << 5);

	//reset
	vdp->line_int_pending = 0;
	vdp->frame_int_pending = 0;
	vdp->sprite_overflow = 0;
	vdp->sprite_collision = 0;

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
		vdp->vdp_control &= 0xC000; //keep code register unchanged
	else
		vdp->vdp_control++;
}

u8 vdpGetCodeRegister(struct Vdp* vdp)
{
	u8 code_register = (vdp->vdp_control >> 14) & 0x3;
	return code_register;
}

u16 vdpGetAddressRegister(struct Vdp* vdp)
{
	u16 address_register = (vdp->vdp_control) & 0x3FFF;
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
	//bit 0 and 7 ignored
	u8 sprite_table_addr = vdp->registers[0x5];
	u16 base_addr = (sprite_table_addr << 7) & 0x3F00;
	
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
