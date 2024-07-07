//
//  main.c
//  s9
//
//  Created by shea on 7/7/24.
//

#include <stdio.h>
#include "s9_app.h"

#include <stdio.h>
#include <SDL2/SDL.h>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#include <GL/gl.h>
#endif

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

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

int main(int argc, char* argv[]) {
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
		return 1;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

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

	#ifndef __APPLE__
	glewExperimental = GL_TRUE;
	GLenum glewError = glewInit();
	if (glewError != GLEW_OK) {
		fprintf(stderr, "Error initializing GLEW: %s\n", glewGetErrorString(glewError));
		SDL_GL_DeleteContext(context);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	#endif

	float vertices[88];
	int indices[44];
	createGrid(vertices, indices);

	GLuint vao, vbo, ebo;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ebo);

	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	GLint posAttrib;
	GLuint shaderProgram = glCreateProgram();
	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vertexSource, NULL);
	glCompileShader(vertexShader);
	glAttachShader(shaderProgram, vertexShader);

	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
	glCompileShader(fragmentShader);
	glAttachShader(shaderProgram, fragmentShader);

	glLinkProgram(shaderProgram);
	glUseProgram(shaderProgram);

	posAttrib = glGetAttribLocation(shaderProgram, "position");
	glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(posAttrib);

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
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	glDeleteBuffers(1, &vbo);
	glDeleteBuffers(1, &ebo);
	glDeleteVertexArrays(1, &vao);

	SDL_GL_DeleteContext(context);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}


int mainx(int argc, const char * argv[]) {	
	s9_app_config_t config = {
		
	};
	
	s9_app_create(&config);
	
	s9_print("hello %d\n", 99);	
	while(s9_app_run() == 0);	
	s9_print("goodbye %d\n", 123);
	
	s9_app_destroy();
	
	return 0;
}
