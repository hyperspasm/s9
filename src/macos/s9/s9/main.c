#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#define GL_GLEXT_PROTOTYPES
const GLchar* vertexSource =
	"#version 150 core\n"
	"in vec2 position;\n"
	"void main() {\n"
	"    gl_Position = vec4(position, 0.0, 1.0);\n"
	"}";
const GLchar* fragmentSource =
	"#version 150 core\n"
	"out vec4 outColor;\n"
	"void main() {\n"
	"    outColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
	"}";
const GLchar* textVertexSource =
	"#version 150 core\n"
	"in vec2 position;\n"
	"in vec2 texcoord;\n"
	"out vec2 Texcoord;\n"
	"void main() {\n"
	//"    Texcoord = texcoord;\n"
	"   Texcoord = vec2(texcoord.x, 1.0 - texcoord.y);\n"
	"    gl_Position = vec4(position, 0.0, 1.0);\n"
	"}";
const GLchar* textFragmentSource =
	"#version 150 core\n"
	"in vec2 Texcoord;\n"
	"out vec4 outColor;\n"
	"uniform sampler2D tex;\n"
	"void main() {\n"
	"    outColor = texture(tex, Texcoord);\n"
	"}";
#else
#include <GLES2/gl2.h>
#include <EGL/egl.h>
const GLchar* vertexSource =
	"attribute vec2 position;\n"
	"void main() {\n"
	"    gl_Position = vec4(position, 0.0, 1.0);\n"
	"}";
const GLchar* fragmentSource =
	"precision mediump float;\n"
	"void main() {\n"
	"    gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
	"}";
const GLchar* textVertexSource =
	"attribute vec2 position;\n"
	"attribute vec2 texcoord;\n"
	"varying vec2 Texcoord;\n"
	"void main() {\n"
	"    Texcoord = texcoord;\n"
	"    gl_Position = vec4(position, 0.0, 1.0);\n"
	"}";
const GLchar* textFragmentSource =
	"precision mediump float;\n"
	"varying vec2 Texcoord;\n"
	"uniform sampler2D tex;\n"
	"void main() {\n"
	"    gl_FragColor = texture2D(tex, Texcoord);\n"
	"}";
#endif

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080

void createGrid(float* vertices, int* indices) {
	int index = 0;
	for (int i = -5; i <= 5; ++i) {
		vertices[index++] = i / 5.0f; vertices[index++] = -1.0f;
		vertices[index++] = i / 5.0f; vertices[index++] =  1.0f;
		vertices[index++] = -1.0f; vertices[index++] = i / 5.0f;
		vertices[index++] =  1.0f; vertices[index++] = i / 5.0f;
	}

	for (int i = 0; i < 44; ++i) {
		indices[i] = i;
	}
}

void checkShaderCompilation(GLuint shader) {
	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
		GLchar infoLog[512];
		glGetShaderInfoLog(shader, 512, NULL, infoLog);
		fprintf(stderr, "Shader Compilation Error: %s\n", infoLog);
	}
}

GLuint compileShader(GLenum type, const char* source) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	checkShaderCompilation(shader);
	return shader;
}

GLuint createProgram(const char* vertexSource, const char* fragmentSource) {
	GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
	GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);

	GLuint program = glCreateProgram();
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);

	GLint status;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (status != GL_TRUE) {
		char buffer[512];
		glGetProgramInfoLog(program, 512, NULL, buffer);
		fprintf(stderr, "Program linking failed: %s\n", buffer);
	}

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	return program;
}

GLuint createTextTexture(const char* text, TTF_Font* font, SDL_Color color, int* w, int* h) {
	SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text, color);
	if (!surface) {
		fprintf(stderr, "Unable to create text surface: %s\n", TTF_GetError());
		return 0;
	}

	*w = surface->w;
	*h = surface->h;
	
	printf("1: %s\n", SDL_GetPixelFormatName(surface->format->format));
	
#if 1
	// Convert surface to a known format (RGBA32)
	SDL_Surface* formattedSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(surface);

	if (!formattedSurface) {
		fprintf(stderr, "Unable to convert text surface: %s\n", SDL_GetError());
		return 0;
	}
#else
	SDL_Surface* formattedSurface = surface;
#endif
	
	printf("2: %s\n", SDL_GetPixelFormatName(formattedSurface->format->format));

	GLuint texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, formattedSurface->w, formattedSurface->h, 0,
				 GL_RGBA, GL_UNSIGNED_BYTE, formattedSurface->pixels);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	GLenum error = glGetError();
	if (error != GL_NO_ERROR) {
		fprintf(stderr, "Error uploading texture: %d\n", error);
	}

	SDL_FreeSurface(formattedSurface);
	return texture;
}

void renderText(GLuint texture, GLuint vao, GLuint vbo, GLuint program, float x, float y, float w, float h) {
	GLfloat vertices[] = {
		x, y, 0.0f, 0.0f,
		x + w, y, 1.0f, 0.0f,
		x + w, y + h, 1.0f, 1.0f,
		x, y + h, 0.0f, 1.0f
	};

	glUseProgram(program);

	glBindTexture(GL_TEXTURE_2D, texture);
	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

	GLint texUniform = glGetUniformLocation(program, "tex");
	glUniform1i(texUniform, 0);

	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

int main(int argc, char* argv[]) {
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
		return 1;
	}

	if (TTF_Init() < 0) {
		fprintf(stderr, "Could not initialize SDL_ttf: %s\n", TTF_GetError());
		SDL_Quit();
		return 1;
	}

#ifdef __APPLE__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#endif

	SDL_Window *window = SDL_CreateWindow("SDL2 OpenGL Grid",
										  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
										  SCREEN_WIDTH, SCREEN_HEIGHT,
										  SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!window) {
		fprintf(stderr, "Could not create window: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDL_GLContext context = SDL_GL_CreateContext(window);
	if (!context) {
		fprintf(stderr, "Could not create OpenGL context: %s\n", SDL_GetError());
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	float vertices[88];
	int indices[44];
	createGrid(vertices, indices);

	GLuint vao, vbo, ebo, textVao, textVbo;

#ifdef __APPLE__
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
#endif

	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ebo);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	GLuint gridShaderProgram = createProgram(vertexSource, fragmentSource);
	glUseProgram(gridShaderProgram);

	GLint posAttrib = glGetAttribLocation(gridShaderProgram, "position");
	glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), 0);
	glEnableVertexAttribArray(posAttrib);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	TTF_Font* font = TTF_OpenFont("/Users/shea/work/s9/src/macos/s9/s9/Roboto-Regular.ttf", 40);
	if (!font) {
		fprintf(stderr, "Could not load font: %s\n", TTF_GetError());
		SDL_GL_DeleteContext(context);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	SDL_Color color = { 255, 255, 255, 255 };
	int textWidth, textHeight;
	GLuint textTexture = createTextTexture("GLuint textTexture = createTextTexture", font, color, &textWidth, &textHeight);

	glGenVertexArrays(1, &textVao);
	glBindVertexArray(textVao);

	glGenBuffers(1, &textVbo);
	glBindBuffer(GL_ARRAY_BUFFER, textVbo);

	GLuint textShaderProgram = createProgram(textVertexSource, textFragmentSource);
	glUseProgram(textShaderProgram);

	GLint textPosAttrib = glGetAttribLocation(textShaderProgram, "position");
	glVertexAttribPointer(textPosAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
	glEnableVertexAttribArray(textPosAttrib);

	GLint texCoordAttrib = glGetAttribLocation(textShaderProgram, "texcoord");
	glVertexAttribPointer(texCoordAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
	glEnableVertexAttribArray(texCoordAttrib);

	int running = 1;
	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				running = 0;
			}
		}

		glClear(GL_COLOR_BUFFER_BIT);

		// Render grid
		glUseProgram(gridShaderProgram);
		glBindVertexArray(vao);
		glDrawElements(GL_LINES, 44, GL_UNSIGNED_INT, 0);

		// Render text
		renderText(textTexture, textVao, textVbo, textShaderProgram, -0.8f, 0.8f, textWidth / (float)SCREEN_WIDTH, textHeight / (float)SCREEN_HEIGHT);

		SDL_GL_SwapWindow(window);
	}

	glDeleteTextures(1, &textTexture);
	TTF_CloseFont(font);
	TTF_Quit();

	glDeleteProgram(gridShaderProgram);
	glDeleteProgram(textShaderProgram);

	glDeleteBuffers(1, &vbo);
	glDeleteBuffers(1, &ebo);
	glDeleteBuffers(1, &textVbo);

#ifdef __APPLE__
	glDeleteVertexArrays(1, &vao);
	glDeleteVertexArrays(1, &textVao);
#endif

	SDL_GL_DeleteContext(context);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
