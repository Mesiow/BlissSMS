#include <stdio.h>
#include <stdlib.h>
#include "Core\System.h"

int main(void) {
	
	struct System sms;

	systemInit(&sms);
	systemRunEmulation(&sms);


	return 0;
}