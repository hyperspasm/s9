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
const int AMPLITUDE_BAR_WIDTH = WINDOW_WIDTH - 40;
const int AMPLITUDE_BAR_HEIGHT = 30;
const int AMPLITUDE_BAR_Y = 20;
const int NUM_CIRCLE_SEGMENTS = 100;

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

#if 1
typedef struct {
	float x;
	float y;
} PointF;

// Function to normalize a vector
void normalize(PointF* v) {
	float length = sqrtf(v->x * v->x + v->y * v->y);
	if (length != 0) {
		v->x /= length;
		v->y /= length;
	}
}

// Function to compute the normal of a segment
PointF compute_normal(SDL_Point p1, SDL_Point p2) {
	float dx = (float)(p2.x - p1.x);
	float dy = (float)(p2.y - p1.y);
	PointF normal;
	float length = sqrtf(dx * dx + dy * dy);

	// Handle small lengths to avoid instability
	if (length > 1e-6) {
		normal.x = dy / length;
		normal.y = -dx / length;
	} else {
		normal.x = 0;
		normal.y = 0;
	}
	return normal;
}

// Function to smooth an array of points using a moving average
void smooth_points(PointF* input_points, PointF* smoothed_points, int num_points, int window_size) {
	for (int i = 0; i < num_points; ++i) {
		int count = 0;
		float sum_x = 0.0f;
		float sum_y = 0.0f;
		for (int j = -window_size; j <= window_size; ++j) {
			int index = i + j;
			if (index >= 0 && index < num_points) {
				sum_x += input_points[index].x;
				sum_y += input_points[index].y;
				count++;
			}
		}
		smoothed_points[i].x = sum_x / count;
		smoothed_points[i].y = sum_y / count;
	}
}

// Function to smooth an array of SDL points using a moving average
void smooth_sdl_points(SDL_Point* input_points, SDL_Point* smoothed_points, int num_points, int window_size) {
	for (int i = 0; i < num_points; ++i) {
		int count = 0;
		float sum_x = 0.0f;
		float sum_y = 0.0f;
		for (int j = -window_size; j <= window_size; ++j) {
			int index = i + j;
			if (index >= 0 && index < num_points) {
				sum_x += input_points[index].x;
				sum_y += input_points[index].y;
				count++;
			}
		}
		smoothed_points[i].x = (int)(sum_x / count);
		smoothed_points[i].y = (int)(sum_y / count);
	}
}

void compute_offset_curve(SDL_Point* input_points, SDL_Point* offset_points, int num_points, float offset, int window_size) {
	if (num_points < 2) return;  // Need at least 2 points to define a curve

	PointF* normals = (PointF*)malloc(num_points * sizeof(PointF));
	PointF* smoothed_normals = (PointF*)malloc(num_points * sizeof(PointF));
	SDL_Point* smoothed_offset_points = (SDL_Point*)malloc(num_points * sizeof(SDL_Point));

	// Compute normals for each segment
	for (int i = 0; i < num_points - 1; ++i) {
		normals[i] = compute_normal(input_points[i], input_points[i + 1]);
	}

	// Handle the first and last normals separately
	normals[0] = compute_normal(input_points[0], input_points[1]);
	normals[num_points - 1] = compute_normal(input_points[num_points - 2], input_points[num_points - 1]);

	// Smooth normals using a moving average with the specified window size
	smooth_points(normals, smoothed_normals, num_points, window_size);

	// Offset each point using the smoothed normals
	for (int i = 0; i < num_points; ++i) {
		normalize(&smoothed_normals[i]); // Ensure normal is unit length
		offset_points[i].x = (int)(input_points[i].x + offset * smoothed_normals[i].x);
		offset_points[i].y = (int)(input_points[i].y + offset * smoothed_normals[i].y);
	}

	// Smooth the offset points
	smooth_sdl_points(offset_points, smoothed_offset_points, num_points, window_size);

	// Copy the smoothed offset points back to offset_points
	for (int i = 0; i < num_points; ++i) {
		offset_points[i] = smoothed_offset_points[i];
	}

	free(normals);
	free(smoothed_normals);
	free(smoothed_offset_points);
}



void drawPolygonCurve(SDL_Renderer* renderer, float thickness, SDL_Point* points, int numPoints) {
	if (numPoints < 2) return;

	SDL_Point* thickPoints = (SDL_Point*)malloc(2 * numPoints * sizeof(SDL_Point));
	if (!thickPoints) return;

	SDL_Point* nextThickPoint = thickPoints;

	// Copy original points to the first half
	for (int i = 0; i < numPoints; ++i) {
		*nextThickPoint++ = points[i];	
	}
	
	SDL_Point *offsetPoints = alloca(sizeof(SDL_Point) * numPoints);
	
	compute_offset_curve(points, offsetPoints, numPoints, 50, 5);

//	thickness = 50;
	
	// Calculate offset points for the second half (thickness)
	for (int i = numPoints - 1; i >= 0; i--) {
		
		*nextThickPoint++ = offsetPoints[i]; 		
	}

	drawPolygonOutline(renderer, thickPoints, numPoints * 2);
//	drawPolygonOutline(renderer, points, numPoints);
//	SDL_RenderDrawLines(renderer, points, numPoints);

	free(thickPoints);
}


#else
void drawPolygonCurve(SDL_Renderer* renderer, float thickness, SDL_Point* points, int numPoints) {
	if (numPoints < 2) return; // Need at least two points to form a line

	// Allocate memory for the expanded array
	int totalPoints = 2 * numPoints;
	SDL_Point* expandedPoints = (SDL_Point*)malloc(totalPoints * sizeof(SDL_Point));
	if (!expandedPoints) return; // Allocation failed

	// Copy the original points to the first half of the array
	for (int i = 0; i < numPoints; ++i) {
		expandedPoints[i] = points[i];
	}

	// Compute the offset points for the second half of the array
	for (int i = 0; i < numPoints; ++i) {
		int j = numPoints - 1 - i;
		float dx, dy, length;
		
		if (j == 0) {
			dx = points[j + 1].x - points[j].x;
			dy = points[j + 1].y - points[j].y;
		} else {
			dx = points[j].x - points[j - 1].x;
			dy = points[j].y - points[j - 1].y;
		}

		length = sqrtf(dx * dx + dy * dy);
		dx /= length;
		dy /= length;

		expandedPoints[numPoints + i].x = points[j].x + (int)(dy * thickness);
		expandedPoints[numPoints + i].y = points[j].y - (int)(dx * thickness);
	}

	// Draw the filled polygon
	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // White color
	SDL_RenderDrawLines(renderer, expandedPoints, totalPoints);
	SDL_RenderDrawLine(renderer, expandedPoints[totalPoints - 1].x, expandedPoints[totalPoints - 1].y, expandedPoints[0].x, expandedPoints[0].y); // Close the polygon

	// Free the allocated memory
	free(expandedPoints);
}
#endif

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
	int mouseWidth, mouseHeight;

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

		// Draw thick sine wave using drawPolygonCurve
		SDL_Point sineWavePoints[WINDOW_WIDTH-100];
		for (int x = 0; x < WINDOW_WIDTH-100; x++) {
			int y = WINDOW_HEIGHT / 2 + (int)(SINE_WAVE_AMPLITUDE * sin(2 * M_PI * x / (WINDOW_WIDTH-100)));
			sineWavePoints[x].x = x+50;
			sineWavePoints[x].y = y;
		}
		drawPolygonCurve(renderer, 20.0f, sineWavePoints, WINDOW_WIDTH-100);

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
		SDL_SetRenderDrawColor(renderer, circleColor.r, circleColor.g, circleColor.b, circleColor.a);
		drawCircle(renderer, mouseX, mouseY, precomputedCircleVertices);

		// Draw filled polygon
		drawPolygon(renderer, vertices, 5);

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
