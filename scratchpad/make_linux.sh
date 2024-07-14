#!/bin/sh

# Set the base directory to the script's directory
BASE_DIR=$(dirname "$0")

# Navigate to the base directory (optional)
cd "$BASE_DIR" || exit

# Compile the program
gcc -I../src -o scratchpad main.c ../src/draw_sdl.c -lSDL2 -lSDL2_ttf -lm