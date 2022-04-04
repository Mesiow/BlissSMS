#pragma once
#include "Util.h"
#include <SFML\Graphics.h>

//Vdp vram memory map
/*
	$4000-------------------------------------------------------------- -
	Sprite info table : contains x, yand tile number for each sprite
	$3F00-------------------------------------------------------------- -
	Screen display : 32x28 table of tile numbers / attributes
	$3800-------------------------------------------------------------- -
	Sprite / tile patterns, 256..447
	$2000-------------------------------------------------------------- -
	Sprite / tile patterns, 0..255
	$0000-------------------------------------------------------------- -
*/

#define DISPLAY_WIDTH 256
#define DISPLAY_HEIGHT 192

enum VdpDisplayState {
	Visible = 0,
	HBlank,
	VBlank
};

enum VdpDisplayMode {
	Mode4,
	Mode2
};

struct Vdp {
	u8 vram[0x4000];
	u8 cram[0x20];

	enum VdpDisplayState state;
	enum vdpDisplayMode mode;

	//Internal vdp registers
	u8 registers[0xB];
	u16 cycles;

	//Vdp Ports
	u16 vdp_control;
	u16 vdp_data;
	u16 vcounter;
	u16 hcounter;

	//value returned when games read from vcount because scanlines go up to 262
	//which a byte cannot return so we need to keep it in the 0 - 255 range.
	//lthough the VDP will go through 262 or 313 scanlines it can only do this by going over scanlines more than once in each frame.
	//This means the vcounter will go from 0 to 255 but some of these will be repeated giving a total of 262. 
	//The VDP will go through 262 or 313 scanlines it can only do this by going over scanlines more than once in each frame.
	//This means the vcounter will go from 0 to 255 but some of these will be repeated giving a total of 262
	u8 vcount_port; 
	u8 line_counter;

	//Flags
	u8 status_flags; //holds frame interrupt pending flag, sprite overflow flag and sprite collision flag
	u8 second_control_write;
	u8 writes_to_vram; //if == 1: writes to data port go to vram,
					  //otherwise writes go to cram
	u8 readbuffer;

	u8 frame_int_pending;
	u8 line_int_pending;

	u8 sprite_overflow;
	u8 sprite_collision;
	u8 y_scroll;

	u8 priority_buffer[DISPLAY_WIDTH]; //used for checking priority of sprites/tiles

	struct sfImage* pixels;
	struct sfTexture* framebuffer;
	struct sfSprite* frame;

	u16 display_width;
	u16 display_height;
	u8 frame_complete;

	struct Io* io;
	struct System* sys;
};

void vdpInit(struct Vdp* vdp);
void vdpFree(struct Vdp* vdp);
void vdpConnectIo(struct Vdp *vdp, struct Io* io);
void vdpUpdate(struct Vdp *vdp, u8 cycles);
void vdpScanlineUpdate(struct Vdp* vdp);
void vdpDisplayGraphics(struct Vdp* vdp, sfRenderWindow *window);
void vdpRender(struct Vdp* vdp);
void vdpRenderBackground(struct Vdp* vdp);
void vdpRenderSprites(struct Vdp* vdp);
void vdpSetMode(struct Vdp* vdp);
void vdpBufferPixels(struct Vdp* vdp);
u8 vdpIsDisplayVisible(struct Vdp* vdp);
u8 vdpIsDisplayActive(struct Vdp* vdp);
u8 vdpFrameComplete(struct Vdp* vdp);

void vdpWriteControlPort(struct Vdp* vdp, u8 value);
void vdpWriteDataPort(struct Vdp* vdp, u8 value);
u8 vdpReadControlPort(struct Vdp* vdp);
u8 vdpReadDataPort(struct Vdp* vdp); 

void vdpIncrementAddressRegister(struct Vdp* vdp);
u8 vdpGetCodeRegister(struct Vdp* vdp); 
u16 vdpGetAddressRegister(struct Vdp* vdp);
u16 vdpGetNameTableBaseAddress(struct Vdp* vdp);
u16 vdpGetSpriteAttributeTableBaseAddress(struct Vdp* vdp);

sfColor vdpGetColor(u8 red, u8 green, u8 blue);
u8 vdpGetColorShade(u8 color);

u8 vdpPendingInterrupts(struct Vdp *vdp);
u8 vdpVBlankIrqReady(struct Vdp* vdp);
u8 vdpLineInterruptReady(struct Vdp* vdp);