#pragma once
#include "Util.h"

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
	u16 vdpControl;
	u16 vdpData;

	//Flags
	u8 status_flags; //holds frame interrupt pending flag, sprite overflow flag and sprite collision flag
	u8 second_control_write;
	u8 writes_to_vram; //if == 1: writes to data port go to vram,
					  //otherwise writes go to cram
	u8 readbuffer;

	struct Io* io;
};

void vdpInit(struct Vdp* vdp);
void vdpConnectIo(struct Vdp *vdp, struct Io* io);
void vdpUpdate(struct Vdp *vdp, s32 cycles);
void vdpRender(struct Vdp* vdp);
void vdpRenderBackground(struct Vdp* vdp);
void vdpRenderSprites(struct Vdp* vdp);
void vdpSetMode(struct Vdp* vdp);

void vdpWriteControlPort(struct Vdp* vdp, u8 value);
void vdpWriteDataPort(struct Vdp* vdp, u8 value);
u8 vdpReadControlPort(struct Vdp* vdp);
u8 vdpReadDataPort(struct Vdp* vdp); 


u8 vdpPendingInterrupts(struct Vdp *vdp);
//Frame interrupt
u8 vdpCheckFrameInterruptEnable(struct Vdp* vdp);
u8 vdpVBlankIrqPending(struct Vdp* vdp);

//Line interrupt
u8 vdpLineInterruptEnable(struct Vdp* vdp);
u8 vdpLineInterruptPending(struct Vdp* vdp);