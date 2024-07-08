#include <stdio.h>
#include <SDL2/SDL.h>

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
#endif

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

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

int main(int argc, char* argv[]) {
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
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
										  SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
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

	float vertices[88];
	int indices[44];
	createGrid(vertices, indices);

	GLuint vao, vbo, ebo;

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

	GLuint shaderProgram = createProgram(vertexSource, fragmentSource);
	glUseProgram(shaderProgram);

	GLint posAttrib = glGetAttribLocation(shaderProgram, "position");
	glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), 0);
	glEnableVertexAttribArray(posAttrib);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	int running = 1;
	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				running = 0;
			}
		}

		glClear(GL_COLOR_BUFFER_BIT);

		glDrawElements(GL_LINES, 44, GL_UNSIGNED_INT, 0);

		SDL_GL_SwapWindow(window);
	}

	glDeleteProgram(shaderProgram);

	glDeleteBuffers(1, &vbo);
	glDeleteBuffers(1, &ebo);

#ifdef __APPLE__
	glDeleteVertexArrays(1, &vao);
#endif

	SDL_GL_DeleteContext(context);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
