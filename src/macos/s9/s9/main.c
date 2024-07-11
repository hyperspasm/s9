#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Define font path
#ifdef __APPLE__
#define FONT_PATH "/Users/shea/work/s9/src/macos/s9/s9/Roboto-Regular.ttf"
#else
#define FONT_PATH "Roboto-Regular.ttf"
#endif

const int WINDOW_WIDTH = 1920;
const int WINDOW_HEIGHT = 1080;
const int SINE_WAVE_AMPLITUDE = WINDOW_HEIGHT / 2 - 10; // Amplitude fills the screen
const int CIRCLE_RADIUS = 50;

void logSDLError(const char* msg) {
	fprintf(stderr, "%s error: %s\n", msg, SDL_GetError());
}

SDL_Texture* createTextTexture(SDL_Renderer* renderer, TTF_Font* font, const char* text, SDL_Color color, int* width, int* height) {
	SDL_Surface* surface = TTF_RenderText_Blended(font, text, color);
	if (surface == NULL) {
		logSDLError("TTF_RenderText");
		return NULL;
	}
	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
	if (texture == NULL) {
		logSDLError("SDL_CreateTextureFromSurface");
		SDL_FreeSurface(surface);
		return NULL;
	}
	*width = surface->w;
	*height = surface->h;
	SDL_FreeSurface(surface);
	return texture;
}

void drawThickSineWave(SDL_Renderer* renderer, int width, int height, int thickness) {
	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // White color
	for (int x = 0; x < width; x++) {
		int y = height / 2 + (int)(SINE_WAVE_AMPLITUDE * sin(2 * M_PI * x / width));
		for (int i = -thickness / 2; i <= thickness / 2; i++) {
			SDL_RenderDrawPoint(renderer, x, y + i);
		}
	}
}

void drawCircle(SDL_Renderer* renderer, int centerX, int centerY, int radius, SDL_Color color) {
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
	const int diameter = (radius * 2);
	int x = (radius - 1);
	int y = 0;
	int tx = 1;
	int ty = 1;
	int error = (tx - diameter);

	while (x >= y) {
		SDL_RenderDrawPoint(renderer, centerX + x, centerY - y);
		SDL_RenderDrawPoint(renderer, centerX + x, centerY + y);
		SDL_RenderDrawPoint(renderer, centerX - x, centerY - y);
		SDL_RenderDrawPoint(renderer, centerX - x, centerY + y);
		SDL_RenderDrawPoint(renderer, centerX + y, centerY - x);
		SDL_RenderDrawPoint(renderer, centerX + y, centerY + x);
		SDL_RenderDrawPoint(renderer, centerX - y, centerY - x);
		SDL_RenderDrawPoint(renderer, centerX - y, centerY + x);

		if (error <= 0) {
			++y;
			error += ty;
			ty += 2;
		}

		if (error > 0) {
			--x;
			tx += 2;
			error += (tx - diameter);
		}
	}
}

int main(int argc, char* argv[]) {
	// Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		logSDLError("SDL_Init");
		return 1;
	}

	// Initialize SDL_ttf
	if (TTF_Init() != 0) {
		logSDLError("TTF_Init");
		SDL_Quit();
		return 1;
	}

	// Create window
	SDL_Window* window = SDL_CreateWindow("SDL2 Text and Sine Wave Rendering",
										  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
										  WINDOW_WIDTH, WINDOW_HEIGHT,
										  SDL_WINDOW_SHOWN);
	if (window == NULL) {
		logSDLError("SDL_CreateWindow");
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	// Create renderer
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (renderer == NULL) {
		logSDLError("SDL_CreateRenderer");
		SDL_DestroyWindow(window);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	// Enable blending mode for anti-aliasing
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	// Load fonts
	TTF_Font* font200 = TTF_OpenFont(FONT_PATH, 200);
	if (font200 == NULL) {
		logSDLError("TTF_OpenFont 200");
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	TTF_Font* font14 = TTF_OpenFont(FONT_PATH, 14);
	if (font14 == NULL) {
		logSDLError("TTF_OpenFont 14");
		TTF_CloseFont(font200);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	Uint32 startTime = SDL_GetTicks();
	int frameCount = 0;

	int textWidth, textHeight;
	SDL_Color colors[2] = { {255, 0, 0, 255}, {0, 255, 0, 255} }; // Red and Green
	SDL_Color currentColor = colors[0];
	SDL_Texture* textTexture = createTextTexture(renderer, font200, "Hello, world!", currentColor, &textWidth, &textHeight);
	if (textTexture == NULL) {
		TTF_CloseFont(font200);
		TTF_CloseFont(font14);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	int mouseX, mouseY;
	SDL_Texture* mouseTexture = NULL;
	int mouseWidth, mouseHeight;

	// Main loop
	int quit = 0;
	SDL_Event e;
	while (!quit) {
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) {
				quit = 1;
			} else if (e.type == SDL_KEYDOWN) {
				printf("Key pressed: %s\n", SDL_GetKeyName(e.key.keysym.sym));
			}
		}

		SDL_GetMouseState(&mouseX, &mouseY);

		// Clear screen
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Black color
		SDL_RenderClear(renderer);

		// Draw thick sine wave
		drawThickSineWave(renderer, WINDOW_WIDTH, WINDOW_HEIGHT, 3);

		// Render "Hello, world!" text
		SDL_Rect textRect = { (WINDOW_WIDTH - textWidth) / 2, (WINDOW_HEIGHT - textHeight) / 2, textWidth, textHeight };
		SDL_RenderCopy(renderer, textTexture, NULL, &textRect);

		// Calculate frame rate
		frameCount++;
		Uint32 elapsedTime = SDL_GetTicks() - startTime;
		float fps = frameCount / (elapsedTime / 1000.0f);

		// Create FPS text surface
		char fpsText[32];
		snprintf(fpsText, sizeof(fpsText), "FPS: %.2f", fps);
		SDL_Surface* fpsSurface = TTF_RenderText_Blended(font14, fpsText, (SDL_Color){255, 255, 255, 255});
		if (fpsSurface == NULL) {
			logSDLError("TTF_RenderText FPS");
			break;
		}

		// Create FPS text texture
		SDL_Texture* fpsTexture = SDL_CreateTextureFromSurface(renderer, fpsSurface);
		if (fpsTexture == NULL) {
			logSDLError("SDL_CreateTextureFromSurface FPS");
			SDL_FreeSurface(fpsSurface);
			break;
		}

		// Render FPS text
		SDL_Rect fpsRect = { 10, 10, fpsSurface->w, fpsSurface->h };
		SDL_RenderCopy(renderer, fpsTexture, NULL, &fpsRect);

		SDL_FreeSurface(fpsSurface);
		SDL_DestroyTexture(fpsTexture);

		// Create mouse coordinates text surface
		char mouseText[32];
		snprintf(mouseText, sizeof(mouseText), "Mouse: (%d, %d)", mouseX, mouseY);
		SDL_Surface* mouseSurface = TTF_RenderText_Blended(font14, mouseText, (SDL_Color){255, 255, 255, 255});
		if (mouseSurface == NULL) {
			logSDLError("TTF_RenderText Mouse");
			break;
		}

		// Create mouse coordinates text texture
		mouseTexture = SDL_CreateTextureFromSurface(renderer, mouseSurface);
		if (mouseTexture == NULL) {
			logSDLError("SDL_CreateTextureFromSurface Mouse");
			SDL_FreeSurface(mouseSurface);
			break;
		}

		// Render mouse coordinates text
		SDL_Rect mouseRect = { WINDOW_WIDTH - mouseSurface->w - 10, 10, mouseSurface->w, mouseSurface->h };
		SDL_RenderCopy(renderer, mouseTexture, NULL, &mouseRect);

		SDL_FreeSurface(mouseSurface);
		SDL_DestroyTexture(mouseTexture);

		// Determine circle color based on mouse button state
		Uint32 mouseState = SDL_GetMouseState(NULL, NULL);
		SDL_Color circleColor;
		if (mouseState & SDL_BUTTON(SDL_BUTTON_LEFT)) {
			circleColor = (SDL_Color){255, 0, 0, 255}; // Red
		} else if (mouseState & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
			circleColor = (SDL_Color){0, 0, 255, 255}; // Blue
		} else {
			circleColor = (SDL_Color){0, 255, 0, 255}; // Green
		}

		// Draw circle outline following the mouse
		drawCircle(renderer, mouseX, mouseY, CIRCLE_RADIUS, circleColor);

		// Present renderer
		SDL_RenderPresent(renderer);

		// Alternate "Hello, world!" color
		currentColor = colors[frameCount % 2];
		SDL_DestroyTexture(textTexture);
		textTexture = createTextTexture(renderer, font200, "Hello, world!", currentColor, &textWidth, &textHeight);
	}

	// Clean up
	SDL_DestroyTexture(textTexture);
	TTF_CloseFont(font200);
	TTF_CloseFont(font14);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	TTF_Quit();
	SDL_Quit();

	return 0;
}
