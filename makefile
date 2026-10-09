
CFLAGS=-std=c23 -Wall -Wextra -Werror

all:
	gcc chip8.c -o chip8 $(CFLAGS) `sdl2-config --cflags --libs`

debug:
	gcc chip8.c -o chip8 $(CFLAGS) `sdl2-config --cflags --libs` -DDEBUG

sdl3:
	gcc chip8_sdl3.c -o chip8 $(CFLAGS) `pkg-config --cflags --libs sdl3`

sdl3_debug:
	gcc chip8_sdl3.c -o chip8 $(CFLAGS) `pkg-config --cflags --libs sdl3` -DDEBUG
