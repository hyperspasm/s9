#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "draw_sdl.h"

int s9_log(int log_level, const char *format, ...) {
	int ret;
	va_list args;	
	
	va_start(args, format);
	ret = vprintf(format, args);
	va_end(args);
	
	return ret;
}


// Define font path
#ifdef __APPLE__
#define FONT_PATH "/Users/shea/work/s9/scratchpad/Roboto-Regular.ttf"
#else
#define FONT_PATH "Roboto-Regular.ttf"
#endif

const int WINDOW_WIDTH = 1920;
const int WINDOW_HEIGHT = 1080;
const int SINE_WAVE_AMPLITUDE = WINDOW_HEIGHT / 2 - 10; // Amplitude fills the screen
const int CIRCLE_RADIUS = 50;
const int AMPLITUDE_BAR_WIDTH = WINDOW_WIDTH - 40;
const int AMPLITUDE_BAR_HEIGHT = 30;
const int AMPLITUDE_BAR_Y = 20;
const int NUM_CIRCLE_SEGMENTS = 100;
const int SINE_WAVE_START_X = 50;
const int SINE_WAVE_END_X = WINDOW_WIDTH - 50;

float amplitude = -60.0f; // Global variable for amplitude in dB
SDL_mutex* amplitude_mutex; // Mutex for amplitude variable

void logSDLError(const char* msg) {
	fprintf(stderr, "%s error: %s\n", msg, SDL_GetError());
}

void printRendererInfo(SDL_Window* window, SDL_Renderer* renderer) {
	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(renderer, &info) != 0) {
		logSDLError("SDL_GetRendererInfo");
		return;
	}

	fprintf(stdout, "Renderer name: %s\n", info.name);
	fprintf(stdout, "Renderer flags:\n");
	if (info.flags & SDL_RENDERER_SOFTWARE) fprintf(stdout, " - Software\n");
	if (info.flags & SDL_RENDERER_ACCELERATED) fprintf(stdout, " - Accelerated\n");
	if (info.flags & SDL_RENDERER_PRESENTVSYNC) fprintf(stdout, " - VSync\n");
	if (info.flags & SDL_RENDERER_TARGETTEXTURE) fprintf(stdout, " - Render to texture\n");

	fprintf(stdout, "Max texture width: %d\n", info.max_texture_width);
	fprintf(stdout, "Max texture height: %d\n", info.max_texture_height);

	int displayIndex = SDL_GetWindowDisplayIndex(window);
	if (displayIndex < 0) {
		logSDLError("SDL_GetWindowDisplayIndex");
		return;
	}

	SDL_DisplayMode displayMode;
	if (SDL_GetCurrentDisplayMode(displayIndex, &displayMode) != 0) {
		logSDLError("SDL_GetCurrentDisplayMode");
		return;
	}

	fprintf(stdout, "Display mode:\n");
	fprintf(stdout, " - Format: %s\n", SDL_GetPixelFormatName(displayMode.format));
	fprintf(stdout, " - Width: %d\n", displayMode.w);
	fprintf(stdout, " - Height: %d\n", displayMode.h);
	fprintf(stdout, " - Refresh rate: %d Hz\n", displayMode.refresh_rate);
	fprintf(stdout, " - Driver data: %p\n", displayMode.driverdata);
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

void drawSineWave(SDL_Renderer* renderer, int startX, int endX, int height, int amplitude, int filled) {
	int width = endX - startX;
	SDL_FPoint points[width];
	
	for (int x = 0; x < width; x++) {
		float y = height / 2 + amplitude * sinf(2 * M_PI * x / width);
		points[x] = (SDL_FPoint){ .x = startX + x, .y = y };
	}
	
	SDL_Color color = {255, 0, 255, 255}; // Magenta color
	draw_sdl_path(renderer, color, 50, filled, points, width);
}

void drawPolygonOutline(SDL_Renderer* renderer, SDL_Point* points, int numPoints);
	
void drawClosedPolygon(SDL_Renderer* renderer, int x, int y, int n, int r, int filled) {	
	SDL_FPoint points[n+1];
	
	double factor = (M_PI * 2.0) / n;
	
	int i;
	
	SDL_FPoint *d = points;
	for(i=0; i<n; i++) {
		double v = i * factor;
		
		d->x = cos(v);
		d->y = sin(v);
		
		d->x *= r;
		d->y *= r;
		
		d->x += x;
		d->y += y;
		
		d++;
	}
	
	// closed
	points[n] = points[0];
	
	SDL_Color color = {255, 255, 0, 255}; // Yellow color
	draw_sdl_path(renderer, color, 30, filled, points, n+1);
	
	SDL_Point pts[n+1];
	for(i=0; i<=n; i++) {
		pts[i].x = points[i].x;
		pts[i].y = points[i].y;
	}
	drawPolygonOutline(renderer, pts, n+1);
	
	
}

void drawPolygon(SDL_Renderer* renderer, SDL_Vertex* vertices, int numVertices) {
	// Triangulate the polygon (assuming it's convex)
	// This example assumes numVertices is at least 3 and that the polygon is convex.
	int numIndices = (numVertices - 2) * 3;
	int* indices = (int*)malloc(numIndices * sizeof(int));

	for (int i = 0; i < numVertices - 2; i++) {
		indices[i * 3] = 0;
		indices[i * 3 + 1] = i + 1;
		indices[i * 3 + 2] = i + 2;
	}

	SDL_RenderGeometry(renderer, NULL, vertices, numVertices, indices, numIndices);

	free(indices);
}

void drawPolygonOutline(SDL_Renderer* renderer, SDL_Point* points, int numPoints) {
	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // White color
	if (numPoints > 1) {
		SDL_RenderDrawLines(renderer, points, numPoints);
		SDL_RenderDrawLine(renderer, points[numPoints - 1].x, points[numPoints - 1].y, points[0].x, points[0].y); // Close the polygon
	}
}

void precomputeCircleVertices(SDL_Point* points, int radius) {
	double angleIncrement = 2 * M_PI / NUM_CIRCLE_SEGMENTS;

	for (int i = 0; i < NUM_CIRCLE_SEGMENTS; ++i) {
		double angle = i * angleIncrement;
		points[i].x = (int)(radius * cos(angle));
		points[i].y = (int)(radius * sin(angle));
	}
}

void drawCircle(SDL_Renderer* renderer, int centerX, int centerY, SDL_Point* precomputedPoints) {
	SDL_Point points[NUM_CIRCLE_SEGMENTS];
	for (int i = 0; i < NUM_CIRCLE_SEGMENTS; ++i) {
		points[i].x = centerX + precomputedPoints[i].x;
		points[i].y = centerY + precomputedPoints[i].y;
	}

	drawPolygonOutline(renderer, points, NUM_CIRCLE_SEGMENTS);
}

void drawAmplitudeBar(SDL_Renderer* renderer, float amplitude) {
	int barLength = (int)((amplitude + 60.0f) / 60.0f * AMPLITUDE_BAR_WIDTH);
	barLength = barLength < 0 ? 0 : (barLength > AMPLITUDE_BAR_WIDTH ? AMPLITUDE_BAR_WIDTH : barLength);
	SDL_Rect barRect = { 20, AMPLITUDE_BAR_Y, barLength, AMPLITUDE_BAR_HEIGHT };

	SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255); // Green color for the bar
	SDL_RenderFillRect(renderer, &barRect);
}

void audio_callback(void *userdata, Uint8 *stream, int len) {
	float* samples = (float*)stream;
	int sampleCount = len / sizeof(float);
	float sum = 0.0f;

	for (int i = 0; i < sampleCount; i++) {
		sum += samples[i] * samples[i];
	}

	float rms = sqrt(sum / sampleCount);
	float db = 20.0f * log10(rms);

	if (db < amplitude) db = amplitude - 0.5f;

	SDL_LockMutex(amplitude_mutex);
	amplitude = db;
	SDL_UnlockMutex(amplitude_mutex);
}

int main(int argc, char* argv[]) {
	// Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
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
	SDL_Window* window = SDL_CreateWindow("SDL2 Audio Input and Amplitude Display",
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

	// Print renderer info
	printRendererInfo(window, renderer);

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

	// Create mutex for amplitude variable
	amplitude_mutex = SDL_CreateMutex();
	if (amplitude_mutex == NULL) {
		logSDLError("SDL_CreateMutex");
		TTF_CloseFont(font200);
		TTF_CloseFont(font14);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	// List available audio devices
	int numAudioDevices = SDL_GetNumAudioDevices(SDL_TRUE);
	fprintf(stdout, "Available audio capture devices:\n");
	for (int i = 0; i < numAudioDevices; ++i) {
		fprintf(stdout, " - %s\n", SDL_GetAudioDeviceName(i, SDL_TRUE));
	}

	const char* audioDeviceName = SDL_GetAudioDeviceName(0, SDL_TRUE);
	if (audioDeviceName == NULL) {
		fprintf(stderr, "No audio capture devices found.\n");
		SDL_DestroyMutex(amplitude_mutex);
		TTF_CloseFont(font200);
		TTF_CloseFont(font14);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	fprintf(stdout, "Using audio capture device: %s\n", audioDeviceName);

	// Setup audio capture
	SDL_AudioSpec desiredSpec, obtainedSpec;
	SDL_zero(desiredSpec);
	desiredSpec.freq = 44100;
	desiredSpec.format = AUDIO_F32; // If this format causes issues, try AUDIO_S16SYS instead
	desiredSpec.channels = 2;
	desiredSpec.samples = 1024;
	desiredSpec.callback = audio_callback;

	SDL_AudioDeviceID audioDevice = SDL_OpenAudioDevice(audioDeviceName, SDL_TRUE, &desiredSpec, &obtainedSpec, 0);
	if (audioDevice == 0) {
		logSDLError("SDL_OpenAudioDevice");
		SDL_DestroyMutex(amplitude_mutex);
		TTF_CloseFont(font200);
		TTF_CloseFont(font14);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	// Start audio capture
	SDL_PauseAudioDevice(audioDevice, 0);

	Uint32 startTime = SDL_GetTicks();
	int frameCount = 0;

	int textWidth, textHeight;
	SDL_Color colors[2] = { {255, 0, 0, 255}, {0, 255, 0, 255} }; // Red and Green
	SDL_Color currentColor = colors[0];
	SDL_Texture* textTexture = createTextTexture(renderer, font200, "Hello, world!", currentColor, &textWidth, &textHeight);
	if (textTexture == NULL) {
		SDL_CloseAudioDevice(audioDevice);
		SDL_DestroyMutex(amplitude_mutex);
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

	// Define a polygon with 5 vertices
	SDL_Vertex vertices[5] = {
		{{500, 300}, {255, 0, 0, 255}, {0, 0}},   // Vertex 1
		{{700, 300}, {0, 255, 0, 255}, {0, 0}},   // Vertex 2
		{{750, 500}, {0, 0, 255, 255}, {0, 0}},   // Vertex 3
		{{600, 600}, {255, 255, 0, 255}, {0, 0}}, // Vertex 4
		{{450, 500}, {0, 255, 255, 255}, {0, 0}}  // Vertex 5
	};

	// Define the same polygon for the outline
	SDL_Point points[5] = {
		{500, 300},
		{700, 300},
		{750, 500},
		{600, 600},
		{450, 500}
	};

	// Precompute circle vertices at the origin
	SDL_Point precomputedCircleVertices[NUM_CIRCLE_SEGMENTS];
	precomputeCircleVertices(precomputedCircleVertices, CIRCLE_RADIUS);

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

		// Render "Hello, world!" text
		SDL_Rect textRect = { (WINDOW_WIDTH - textWidth) / 2, (WINDOW_HEIGHT - textHeight) / 2, textWidth, textHeight };
		SDL_RenderCopy(renderer, textTexture, NULL, &textRect);
		
		// Draw filled polygon
		drawPolygon(renderer, vertices, 5);
		
		// Draw thick sine wave
		drawSineWave(renderer, SINE_WAVE_START_X, SINE_WAVE_END_X, WINDOW_HEIGHT, SINE_WAVE_AMPLITUDE, 0);
		
		drawClosedPolygon(renderer, 350, 350, 6, 300, 0);

		

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
		SDL_SetRenderDrawColor(renderer, circleColor.r, circleColor.g, circleColor.b, circleColor.a);
		drawCircle(renderer, mouseX, mouseY, precomputedCircleVertices);

		// Draw polygon outline
		drawPolygonOutline(renderer, points, 5);

		// Draw amplitude bar
		SDL_LockMutex(amplitude_mutex);
		drawAmplitudeBar(renderer, amplitude);
		SDL_UnlockMutex(amplitude_mutex);

		// Present renderer
		SDL_RenderPresent(renderer);

		// Alternate "Hello, world!" color
		currentColor = colors[frameCount % 2];
		SDL_DestroyTexture(textTexture);
		textTexture = createTextTexture(renderer, font200, "Hello, world!", currentColor, &textWidth, &textHeight);
	}

	// Clean up
	SDL_CloseAudioDevice(audioDevice);
	SDL_DestroyMutex(amplitude_mutex);
	SDL_DestroyTexture(textTexture);
	TTF_CloseFont(font200);
	TTF_CloseFont(font14);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	TTF_Quit();
	SDL_Quit();

	return 0;
}
