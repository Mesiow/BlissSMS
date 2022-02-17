#pragma once
#include <stdio.h>
#include <string.h>

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;

//Returns number of set bits in value
u8 popcount(u8 value);