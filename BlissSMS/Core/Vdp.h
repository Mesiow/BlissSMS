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

struct Vdp {
	u8 tilePatterns0[0x2000];
	u8 tilePatterns1[0x1800];
	u8 screenDisplay[0x700];
	u8 spriteInfo[0x100];

	struct Io* io;
};

void vdpInit(struct Vdp* vdp);
void vdpConnectIo(struct Vdp *vdp, struct Io* io);