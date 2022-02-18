#include <stdio.h>
#include <stdlib.h>
#include "SDL.h"
#include "Core\System.h"

int main(int argc, char *argv[]) {

	if (SDL_Init(SDL_INIT_EVERYTHING)) {
		printf("Failed to init SDL, error: %s\n", SDL_GetError());
		return -1;
	}
	
	const int width = 640;
	const int height = 480;

	SDL_Window* window = SDL_CreateWindow("BlissSMS", SDL_WINDOWPOS_CENTERED, 
		SDL_WINDOWPOS_CENTERED, width, height, 0);
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	
	struct System sms;
	systemInit(&sms);

	u8 running = 1;
	while (running) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			switch (ev.type) {
				case SDL_QUIT: running = 0; break;
			}
		}

		systemRunEmulation(&sms);

		SDL_SetRenderDrawColor(renderer, 22, 100, 135, 255);
		SDL_RenderClear(renderer);

		SDL_RenderPresent(renderer);
	}


	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}