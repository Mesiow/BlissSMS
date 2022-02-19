#include "Util.h"

u8 popcount(u8 value)
{
	u8 count = 0;
	while (value) {
		value &= (value - 1);
		count++;
	}
	return count;
}

u8 setBit(u8 val, u8 bit)
{
	return (val | (1 << bit));
}

u8 clearBit(u8 val, u8 bit)
{
	return (val & ~(1 << bit));
}
