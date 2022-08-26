CFLAGS=-std=c17 -Wall -Wextra 

all:
	gcc chip8.c -o chip8 $(CFLAGS) `sdl2-config --cflags --libs`

debug:
	gcc chip8.c -o chip8 -DDEBUG $(CFLAGS) `sdl2-config --cflags --libs`
