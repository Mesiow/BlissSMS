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
