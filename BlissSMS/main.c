#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "SFML\Graphics.h"
#include "Core\System.h"


int main(int argc, char *argv[]) {
	sfVideoMode mode = { 512, 384, 32 };
	sfRenderWindow* window = sfRenderWindow_create(mode, "BlissSMS", sfResize | sfClose, NULL);
	if (!window) {
		printf("CSFML Window failed to create\n");
		return EXIT_FAILURE;
	}

	struct System sms;
	systemInit(&sms);

	u8 running = 1;
	sfEvent ev;
	while (sfRenderWindow_isOpen(window)) {
		while (sfRenderWindow_pollEvent(window, &ev)) {
			if (ev.type == sfEvtClosed) {
				sfRenderWindow_close(window);
			}
		}

		systemRunEmulation(&sms);

		sfRenderWindow_clear(window, sfBlack);

		systemRenderGraphics(&sms, window);

		sfRenderWindow_display(window);

	}

	systemFree(&sms);


	return 0;
}