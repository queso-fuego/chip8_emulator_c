#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "SDL.h"

#define WINDOW_WIDTH 64
#define WINDOW_HEIGHT 32

#define CHIP8_XRES 64
#define CHIP8_YRES 32

#define SCHIP_XRES 128
#define SCHIP_YRES 64

// CHIP8 Instruction
typedef struct {
    uint16_t opcode;        // The full original 16 bit opcode
    uint16_t NNN;           // 12 bit constant (address)
    uint8_t NN;             // 8 bit constant
    uint8_t N;              // 4 bit constant
    uint8_t X;              // 4 bit Register identifier  
    uint8_t Y;              // 4 bit Register identifier
} inst_t;

// CHIP8 Machine
typedef struct {
    uint8_t ram[4096];                      // Memory
    bool display[CHIP8_XRES * CHIP8_YRES];  // Pixel state on or off
    uint16_t PC;                            // Program counter
    uint16_t I;                             // Index register
    uint16_t stack[32];                     // Subroutine address stack
    uint8_t stack_ptr;                      // Current position on subroutine address stack
    uint8_t delay_timer;                    // Will decrement at 60hz if > 0
    uint8_t sound_timer;                    // Will decrement at 60hz and beep if > 0 
    uint8_t V[16];                          // Registers/variables V0-VF
    bool keypad[16];                        // Keypad state pressed or not pressed
    inst_t inst;                            // Currently or just emulated instruction
    bool draw_flag;                         // If true, update SDL window with changes
    bool paused;                            // Is the machine paused or not? Debugging, breakpoints, etc.
} chip8_t;

// Emulator configuration options
typedef struct {
    uint32_t foreground_color;              // CHIP8 Foreground/pixel color
    uint32_t background_color;              // CHIP8 Background color
    uint32_t scale_factor;                  // CHIP8 Display scale factor
    // TODO: keypad setup
    // TODO: super chip8 yes/no
    uint32_t insts_per_second;              // Number of emulated instructions per second
    bool draw_pixel_outlines;               // Draw pixel outlines/boundaries for debugging or aesthetic purposes
    int square_wave_freq;                   // Audio square wave frequency 
} config_t;

// Initialize configuration defaults
config_t config = {0};

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_AudioSpec want, have;
SDL_AudioDeviceID dev = 0;

// CHIP8 FONT
const uint8_t font[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

// SDL Audio spec callback function, which will be called when more audio data is needed
//   buffer (stream) is handled by SDL, no need to malloc.
void audio_callback(void *userdata, uint8_t *stream, int len) {
    (void)userdata; // Silence compiler warnings

    int16_t *data = (int16_t *)stream;
    static uint32_t running_sample_index = 0;
    const int volume = 3000;    // Value between int min and int max, wave amplitude or loudness
    const int square_wave_period = have.freq / config.square_wave_freq;
    const int half_square_wave_period = square_wave_period / 2;

    // Len is for uint8_t bytes in stream, we are writing 2 bytes at a time (int16_t), so
    //   only need to go up to len / 2
    for (int i = 0; i < len / 2; i++) {
        data[i] = ((running_sample_index++ / half_square_wave_period) % 2) ? volume : -volume;
    }
}

// Initial SDL setup
void init_SDL(const uint32_t scale_factor) {
    // SDL subsystem initialization
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "Could not initialize SDL Video, Audio, and Timer Subsystems :( %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    // Window
    window = SDL_CreateWindow("Chip8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
                              WINDOW_WIDTH * scale_factor, WINDOW_HEIGHT * scale_factor, 0);
    if (!window) {
        fprintf(stderr, "Could not create SDL window %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    // Renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (!renderer) {
        fprintf(stderr, "Could not create SDL renderer %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    // Audio
    memset(&want, 0, sizeof(want)); // Initialize audio spec

    want.freq = 44100;              // CD quality, 44100hz
    want.format = AUDIO_S16SYS;     // Signed 16 bit bytes
    want.channels = 1;              // Mono sound   
    want.samples = want.freq / 20;  // Power of 2 size of audio buffer in samples 
    want.callback = audio_callback; // Function to call to fill in audio buffer

    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev == 0) {
        fprintf(stderr, "Could not open an audio device! %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    if (want.format != have.format) {
        fprintf(stderr, "Could not get desired audio format!\n");
        exit(EXIT_FAILURE);
    }
}

// Final program cleanup before exit (will be called on exit() and normal termination)
void cleanup(void) {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_CloseAudioDevice(dev);
    SDL_Quit();
}

// Clear screen to background color 
void clear_screen(const config_t config) {
    // Set background color
    const uint8_t r = (config.background_color >> 24) & 0xFF;
    const uint8_t b = (config.background_color >> 16) & 0xFF;
    const uint8_t g = (config.background_color >> 8)  & 0xFF;

    SDL_SetRenderDrawColor(renderer, r, g, b, SDL_ALPHA_OPAQUE);    
    SDL_RenderClear(renderer);
}

// Present any new changes to the chip8 display 
void draw_display(const chip8_t chip8, const config_t config) {
    SDL_Rect pixel = {.x = 0, .y = 0, .w = config.scale_factor, .h = config.scale_factor};

    // Check all chip8 pixels, if any are set on, draw it
    for (uint32_t i = 0; i < sizeof(chip8.display); i++) {
        // Translate 1D i value into 2D X,Y coordinates
        // 2D -> 1D = i = Y*CHIP8_XRES + X
        // 1D -> 2D = 
        //   X = i % CHIP8_XRES
        //   Y = i / CHIP8_XRES
        pixel.x = (i % CHIP8_XRES) * config.scale_factor;   
        pixel.y = (i / CHIP8_XRES) * config.scale_factor;  

        if (chip8.display[i]) {
            // Pixel is on, Draw foreground color chip8 pixel
            uint8_t r = (config.foreground_color >> 24) & 0xFF;
            uint8_t g = (config.foreground_color >> 16) & 0xFF;
            uint8_t b = (config.foreground_color >> 8)  & 0xFF;
            SDL_SetRenderDrawColor(renderer, r, g, b, SDL_ALPHA_OPAQUE);    

            SDL_RenderFillRect(renderer, &pixel);   // Draw filled rectangle with color

            if (config.draw_pixel_outlines) {
                // Draw pixel "outline" over the filled rect
                uint8_t r = (config.background_color >> 24) & 0xFF;
                uint8_t g = (config.background_color >> 16) & 0xFF;
                uint8_t b = (config.background_color >> 8)  & 0xFF;
                SDL_SetRenderDrawColor(renderer, r, g, b, SDL_ALPHA_OPAQUE);    

                SDL_RenderDrawRect(renderer, &pixel);   // Draw rectangle outline with background color
            }

        } else {  
            // Pixel is off, Draw background color chip8 pixel
            uint8_t r = (config.background_color >> 24) & 0xFF;
            uint8_t g = (config.background_color >> 16) & 0xFF;
            uint8_t b = (config.background_color >> 8)  & 0xFF;
            SDL_SetRenderDrawColor(renderer, r, g, b, SDL_ALPHA_OPAQUE);    

            SDL_RenderFillRect(renderer, &pixel);   // Draw filled rectangle with color
        }
    }
    
    SDL_RenderPresent(renderer);
}

// Handle user input (keyboard, mouse, etc.)
// ------------------------------------
// CHIP8 Keypad layout | QWERTY layout
// ------------------------------------
// 123C                | 1234
// 456D                | qwer
// 789E                | asdf
// A0BF                | zxcv
// ------------------------------------
void handle_input(chip8_t *chip8) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                exit(EXIT_SUCCESS);
                break;

            case SDL_KEYDOWN:
                switch(event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        // Escape key
                        exit(EXIT_SUCCESS);
                        break;

                    case SDLK_SPACE:
                        // Pause/unpause emulation
                        chip8->paused = !chip8->paused;
                        if (chip8->paused) puts("==== PAUSED ====");   // Show paused text on stdout for user
                        break;

                    // Handle CHIP8 keypad, QWERTY edition
                    case SDLK_1:
                        chip8->keypad[0x1] = true;
                        break;
                    case SDLK_2:
                        chip8->keypad[0x2] = true;
                        break;
                    case SDLK_3:
                        chip8->keypad[0x3] = true;
                        break;
                    case SDLK_4:
                        chip8->keypad[0xC] = true;
                        break;
                    case SDLK_q:
                        chip8->keypad[0x4] = true;
                        break;
                    case SDLK_w:
                        chip8->keypad[0x5] = true;
                        break;
                    case SDLK_e:
                        chip8->keypad[0x6] = true;
                        break;
                    case SDLK_r:
                        chip8->keypad[0xD] = true;
                        break;
                    case SDLK_a:
                        chip8->keypad[0x7] = true;
                        break;
                    case SDLK_s:
                        chip8->keypad[0x8] = true;
                        break;
                    case SDLK_d:
                        chip8->keypad[0x9] = true;
                        break;
                    case SDLK_f:
                        chip8->keypad[0xE] = true;
                        break;
                    case SDLK_z:
                        chip8->keypad[0xA] = true;
                        break;
                    case SDLK_x:
                        chip8->keypad[0x0] = true;
                        break;
                    case SDLK_c:
                        chip8->keypad[0xB] = true;
                        break;
                    case SDLK_v:
                        chip8->keypad[0xF] = true;
                        break;

                    default:
                        break;
                }
                break;

            case SDL_KEYUP:
                switch(event.key.keysym.sym) {
                    // Handle CHIP8 keypad, QWERTY edition
                    case SDLK_1:
                        chip8->keypad[0x1] = false;
                        break;
                    case SDLK_2:
                        chip8->keypad[0x2] = false;
                        break;
                    case SDLK_3:
                        chip8->keypad[0x3] = false;
                        break;
                    case SDLK_4:
                        chip8->keypad[0xC] = false;
                        break;
                    case SDLK_q:
                        chip8->keypad[0x4] = false;
                        break;
                    case SDLK_w:
                        chip8->keypad[0x5] = false;
                        break;
                    case SDLK_e:
                        chip8->keypad[0x6] = false;
                        break;
                    case SDLK_r:
                        chip8->keypad[0xD] = false;
                        break;
                    case SDLK_a:
                        chip8->keypad[0x7] = false;
                        break;
                    case SDLK_s:
                        chip8->keypad[0x8] = false;
                        break;
                    case SDLK_d:
                        chip8->keypad[0x9] = false;
                        break;
                    case SDLK_f:
                        chip8->keypad[0xE] = false;
                        break;
                    case SDLK_z:
                        chip8->keypad[0xA] = false;
                        break;
                    case SDLK_x:
                        chip8->keypad[0x0] = false;
                        break;
                    case SDLK_c:
                        chip8->keypad[0xB] = false;
                        break;
                    case SDLK_v:
                        chip8->keypad[0xF] = false;
                        break;

                    default:
                        break;
                }

            default:
                break;
        }
    }
}

// Initialize new chip8 machine
chip8_t init_chip8(const char rom_name[]) {
    chip8_t *chip8 = calloc(1, sizeof(chip8_t));

    // Load font to chip8 memory
    memcpy(chip8->ram, font, sizeof(font));

    // Load initial rom file to chip8 memory
    FILE *rom = fopen(rom_name, "rb");

    if (!rom) {
        fprintf(stderr, "Rom file %s does not exist!\n", rom_name);
        exit(EXIT_FAILURE);
    }

    fseek(rom, 0, SEEK_END);            // Get file size
    long rom_size = ftell(rom);         // ...  
    rewind(rom);                        // ...

    fread(&chip8->ram[512], rom_size, 1, rom);
    fclose(rom);

    chip8->PC = 512;     // Program counter will start at address 0x200 for ROM

    return *chip8;
}

void print_debug_info(const uint16_t address, const chip8_t chip8) {
    printf("Address: 0x%04X, Opcode: 0x%04X, Desc: ", address, chip8.inst.opcode);

    // Print instruction info
    // The top 4 bits of the opcode are the "category" or "class" of opcode
    switch ((chip8.inst.opcode >> 12) & 0x0F) {
        case 0x00:
            if (chip8.inst.opcode == 0x00E0) {
                printf("Clear screen");

            } else if (chip8.inst.opcode == 0x00EE) {
                // Return from subroutine
                // Pop the return address off of the subroutine address stack into PC
                //   so that the next executed opcode is at the return address
                printf("Return from subroutine (return address: 0x%04X)",
                       chip8.stack[chip8.stack_ptr - 1]);
                
            } else {
                // 0x0NNN, call RCA 1802 machine routine
                // UNIMPLEMENTED! 
                printf("Unimplemented opcode");
            }
            break;

        case 0x01:
            // 0x1NNN: Jump to address NNN
            printf("Jump to address 0x%04X", 
                   chip8.inst.NNN);
            break;

        case 0x02:
            // 0x2NNN: Call subroutine at NNN
            printf("Call subroutine at 0x%04X",
                   chip8.inst.NNN);
            break;

        case 0x03:
            // 0x3XNN: Skips next instruction if VX == NN
            printf("Skip next instruction if V%X (0x%02X) == 0x%02X", 
                   chip8.inst.X, chip8.V[chip8.inst.X], chip8.inst.NN);
            break;

        case 0x04:
            // 0x4XNN: Skips next instruction if VX != NN
            printf("Skip next instruction if V%X (0x%02X) != 0x%02X", 
                   chip8.inst.X, chip8.V[chip8.inst.X], chip8.inst.NN);
            break;

        case 0x05:
            // 0x5XY0: Skips next instruction if VX == VY
            printf("Skip next instruction if V%X (0x%02X) == V%X (0x%02X)", 
                   chip8.inst.X, chip8.V[chip8.inst.X], chip8.inst.Y, chip8.V[chip8.inst.Y]);
            break;

        case 0x06:
            // 0x6XNN: Set register VX = NN
            printf("Set register V%X = %02X", 
                   chip8.inst.X, chip8.inst.NN);
            break;

        case 0x07:
            // 0x7XNN: Set register VX += NN
            printf("Set register V%X += %02X (Result: V%X = 0x%02X)", 
                   chip8.inst.X, chip8.inst.NN, chip8.inst.X, chip8.V[chip8.inst.X]);
            break;

        case 0x08:
            switch (chip8.inst.N) {
                case 0x00:
                    // 0x8XY0: Set register VX = VY
                    printf("Set V%X = V%X (0x%02X)",
                           chip8.inst.X, chip8.inst.Y, chip8.V[chip8.inst.Y]);
                    break;

                case 0x01:
                    // 0x8XY1: Set register VX |= VY (bitwise OR)
                    printf("Set V%X (0x%02X) |= V%X (0x%02X)",
                           chip8.inst.X, chip8.V[chip8.inst.X], chip8.inst.Y, chip8.V[chip8.inst.Y]);
                    break;

                case 0x02:
                    // 0x8XY2: Set register VX &= VY (bitwise AND)
                    printf("Set V%X (0x%02X) &= V%X (0x%02X)",
                           chip8.inst.X, chip8.V[chip8.inst.X], chip8.inst.Y, chip8.V[chip8.inst.Y]);
                    break;

                case 0x03:
                    // 0x8XY3: Set register VX ^= VY (bitwise XOR)
                    printf("Set V%X (0x%02X) ^= V%X (0x%02X)",
                           chip8.inst.X, chip8.V[chip8.inst.X], chip8.inst.Y, chip8.V[chip8.inst.Y]);
                    break;

                case 0x04:
                    // 0x8XY4: Set register VX += VY, VF set if carry or 0 if not
                    printf("Set V%X (0x%02X) += V%X (0x%02X), VF = %d",
                           chip8.inst.X, chip8.V[chip8.inst.X], chip8.inst.Y, chip8.V[chip8.inst.Y], 
                           (chip8.V[chip8.inst.X] + chip8.V[chip8.inst.Y] > UINT8_MAX));
                    break;

                case 0x05:
                    // 0x8XY5: set register VX -= VY, VF set if no borrow (positive result) or 0 if there is a borrow (negative result) 
                    printf("Set V%X (0x%02X) -= V%X (0x%02X), VF = %d (1 = no borrow)",
                           chip8.inst.X, chip8.V[chip8.inst.X], chip8.inst.Y, chip8.V[chip8.inst.Y], 
                           (chip8.V[chip8.inst.X] - chip8.V[chip8.inst.Y] >= 0));
                    break;

                case 0x06:
                    // 0x8XY6: set register VX >>= 1, store shifted off bit in VF (LSB)
                    printf("Set V%X (0x%02X) >>= 1 (result: 0x%02X), VF = shifted off bit (LSB: %d)",
                           chip8.inst.X, chip8.V[chip8.inst.X], chip8.V[chip8.inst.X] >> 1, chip8.V[chip8.inst.X] & 1);
                    break;

                case 0x07:
                    // 0x8XY7: set register VX = VY - VX, VF set if no borrow (positive result) or 0 if there is a borrow (negative result) 
                    printf("Set V%X (0x%02X) = V%X (0x%02X) - V%X, VF = %d (1 = no borrow)",
                           chip8.inst.X, chip8.V[chip8.inst.X], chip8.inst.Y, chip8.V[chip8.inst.Y], chip8.inst.X,
                           (chip8.V[chip8.inst.Y] - chip8.V[chip8.inst.X] >= 0));
                    break;

                case 0x0E:
                    // 0x8XYE: set register VX <<= 1, store shifted off bit in VF (MSB)
                    printf("Set V%X (0x%02X) <<= 1 (result: 0x%02X), VF = shifted off bit (MSB: %d)",
                           chip8.inst.X, chip8.V[chip8.inst.X], chip8.V[chip8.inst.X] << 1, (chip8.V[chip8.inst.X] & 0x80) >> 7);
                    break;

                default:
                    // UNIMPLEMENTED
                    printf("Unimplemented opcode");
                    break;
            }
            break;

        case 0x09:
            // 0x9XY0: Skips next instruction if VX != VY
            printf("Skip next instruction if V%X (0x%02X) != V%X (0x%02X)", 
                   chip8.inst.X, chip8.V[chip8.inst.X], chip8.inst.Y, chip8.V[chip8.inst.Y]);
            break;

        case 0x0A:
            // 0xANNN: Set index register I = NN
            printf("Set index register I = 0x%04X",
                   chip8.inst.NNN);
            break;

        case 0x0B:
            // 0xBNNN: Set PC to V0 + NNN, jumps to address NNN + V0
            printf("Set PC to V0 (0x%02X) + 0x%04X",
                   chip8.V[0], chip8.inst.NNN);
            break;

        case 0x0C:
            // 0xCXNN: Set VX to rand() 0-255 inclusive bitwise ANDed with NN
            printf("Set V%X to rand() 0-255 inclusive bitwise ANDed with 0x%02X",
                   chip8.inst.X, chip8.inst.NN);
            break;

        case 0x0D: {
            // 0xDXYN: Draw/display N pixel high sprite from memory at index register I, at screen coordinates
            //   VX and VY
            printf("Draw 0x%02X pixel high sprite from ram at index register I (0x%04X), at coordinates V%02X (0x%02X), V%02X (0x%02X)",
                   chip8.inst.N, chip8.I, chip8.inst.X, chip8.V[chip8.inst.X], chip8.inst.Y, chip8.V[chip8.inst.Y]);
        }
        break;

        case 0x0E:
            switch (chip8.inst.NN) {
                case 0x9E:
                    // 0xEX9E: Skips the next intruction if the key in VX is pressed (the data in VX will be 0x0-0xF)
                    printf("Skip next instruction if key V%X (0x%02X) is pressed, key pressed: %d",
                           chip8.inst.X, chip8.V[chip8.inst.X], chip8.keypad[chip8.V[chip8.inst.X]]);
                    break;

                case 0xA1:
                    // 0xEXA1: Skips the next intruction if the key in VX is not pressed (the data in VX will be 0x0-0xF)
                    printf("Skip next instruction if key V%X (0x%02X) is not pressed, key pressed: %d",
                           chip8.inst.X, chip8.V[chip8.inst.X], chip8.keypad[chip8.V[chip8.inst.X]]);
                    break;

                default:
                    break;
            }
            break;

        case 0x0F:
            switch (chip8.inst.NN) {
                case 0x07:
                    // FX07: Set VX to value of delay timer
                    printf("Set V%X to delay timer (0x%02X)",
                           chip8.inst.X, chip8.delay_timer);
                    break;

                case 0x0A:
                    // FX0A: Await keypress, store in VX when pressed
                    printf("Await keypress, store in V%X", chip8.inst.X);
                    break;

                case 0x15:
                    // FX15: Set delay timer to VX value
                    printf("Set delay timer to V%X (0x%02X)",
                           chip8.inst.X, chip8.V[chip8.inst.X]);
                    break;

                case 0x18:
                    // FX18: Set sound timer to VX value
                    printf("Set sound timer to V%X (0x%02X)",
                           chip8.inst.X, chip8.V[chip8.inst.X]);
                    break;

                case 0x1E:
                    // FX1E: Add VX to I, I += VX
                    printf("I (0x%04X) += V%X (0x%02X)", 
                           chip8.I, chip8.inst.X, chip8.V[chip8.inst.X]);
                    break;

                case 0x29:
                    // FX29: Set index register I to location of sprite in VX
                    printf("Set I to sprite location in V%X (0x%02X), will multiply by 5: (0x%02X) * 5 = 0x%04X",
                           chip8.inst.X, chip8.V[chip8.inst.X], chip8.V[chip8.inst.X], chip8.V[chip8.inst.X] * 5);
                    break;

                case 0x33:
                    // FX33: Store BCD representation of data in VX at I (hundreds), I+1 (tens), and I+2 (ones)
                    // e.g. VX = 123, I+2 = 3, I+1 = 2, I = 1
                    printf("Store BCD data in V%X (%d) at I (0x%04X)",
                           chip8.inst.X, chip8.V[chip8.inst.X], chip8.I);
                    break;

                case 0x55:
                    // FX55: Store registers V0-VX inclusive in memory, starting from index register I
                    //   I is not incremented, but is offset from
                    printf("Store V0-V%X inclusive in memory starting at I (0x%04X)",
                           chip8.inst.X, chip8.I);
                    break;

                case 0x65:
                    // FX65: Load registers V0-VX inclusive from memory, starting from index register I
                    //   I is not incremented, but is offset from
                    printf("Load V0-V%X inclusive from memory starting at I (0x%04X)",
                           chip8.inst.X, chip8.I);
                    break;

                default:
                    break;
            }
            break;

        default:
            printf("Unimplemented opcode");
            break;
    }

    putchar('\n');
}

// Emulate 1 CHIP8 instruction
void emulate_instruction(chip8_t *chip8) {
    // Get next opcode from memory, opcodes are Big Endian
    chip8->inst.opcode = (chip8->ram[chip8->PC] << 8) | (chip8->ram[chip8->PC+1]); 
    chip8->PC += 2;    // Increment for next opcode, Each opcode is 2 bytes

    uint16_t orig_PC = chip8->PC - 2;

    // Parse out instruction values 
    // 0x600C -> NN = 0C
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN  = chip8->inst.opcode & 0xFF;
    chip8->inst.N   = chip8->inst.opcode & 0x0F;
    chip8->inst.X   = (chip8->inst.opcode >> 8) & 0x0F;
    chip8->inst.Y   = (chip8->inst.opcode >> 4) & 0x0F;

#ifdef DEBUG
        // Print out human readable info for currently emulated instruction
        print_debug_info(orig_PC, *chip8);
#endif

    // The top 4 bits of the opcode are the "category" or "class" of opcode
    switch ((chip8->inst.opcode >> 12) & 0x0F) {
        case 0x00:
            if (chip8->inst.opcode == 0x00E0) {
                // Clear screen, set all display pixels to "off" or false
                memset(chip8->display, false, sizeof(chip8->display));
                chip8->draw_flag = true; // Will update display on next 60hz tick

            } else if (chip8->inst.opcode == 0x00EE) {
                // Return from subroutine
                // Pop the return address off of the subroutine address stack into PC
                //   so that the next executed opcode is at the return address
                chip8->PC = chip8->stack[--chip8->stack_ptr];
                
            } else {
                // 0x0NNN, call RCA 1802 machine routine
                // UNIMPLEMENTED! 
            }
            break;

        case 0x01:
            // 0x1NNN: Jump to address NNN
            // Store NNN in program counter, so next opcode executed is at address NNN
            chip8->PC = chip8->inst.NNN;
            break;

        case 0x02:
            // 0x2NNN: Call subroutine at NNN
            // Store current address to return to, on subroutine address stack
            chip8->stack[chip8->stack_ptr++] = chip8->PC;

            // Called subroutine starts at NNN, start executing code there on next opcode
            chip8->PC = chip8->inst.NNN;    
            break;

        case 0x03:
            // 0x3XNN: Skips next instruction if VX == NN
            if (chip8->V[chip8->inst.X] == chip8->inst.NN) chip8->PC += 2;   // Increment past current (next) opcode, skipping the instruction
            break;

        case 0x04:
            // 0x4XNN: Skips next instruction if VX != NN
            if (chip8->V[chip8->inst.X] != chip8->inst.NN) chip8->PC += 2;   // Increment past current (next) opcode, skipping the instruction
            break;

        case 0x05:
            // 0x5XY0: Skips next instruction if VX == VY
            if (chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]) chip8->PC += 2;   // Increment past current (next) opcode, skipping the instruction
            break;

        case 0x06:
            // 0x6XNN: Set register VX = NN
            chip8->V[chip8->inst.X] = chip8->inst.NN;
            break;

        case 0x07:
            // 0x7XNN: Set register VX += NN
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;

        case 0x08:
            switch (chip8->inst.N) {
                case 0x00:
                    // 0x8XY0: Set register VX = VY
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
                    break;

                case 0x01:
                    // 0x8XY1: Set register VX |= VY (bitwise OR)
                    chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
                    break;

                case 0x02:
                    // 0x8XY2: Set register VX &= VY (bitwise OR)
                    chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
                    break;

                case 0x03:
                    // 0x8XY3: Set register VX ^= VY (bitwise XOR)
                    chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
                    break;

                case 0x04:
                    // 0x8XY4: Set register VX += VY, VF set if carry or 0 if not
                    chip8->V[0xF] = (chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y] > UINT8_MAX);

                    chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
                    break;

                case 0x05:
                    // 0x8XY5: set register VX -= VY, VF set if no borrow (positive result) or 0 if there is a borrow (negative result) 
                    chip8->V[0xF] = (chip8->V[chip8->inst.X] - chip8->V[chip8->inst.Y] >= 0);

                    chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
                    break;

                case 0x06:
                    // 0x8XY6: set register VX >>= 1, store shifted off bit in VF (LSB)
                    chip8->V[0xF] = chip8->V[chip8->inst.X] & 1;

                    chip8->V[chip8->inst.X] >>= 1;
                    break;

                case 0x07:
                    // 0x8XY7: set register VX = VY - VX, VF set if no borrow (positive result) or 0 if there is a borrow (negative result) 
                    chip8->V[0xF] = (chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X] >= 0);

                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                    break;

                case 0x0E:
                    // 0x8XYE: set register VX <<= 1, store shifted off bit in VF (MSB)
                    chip8->V[0xF] = (chip8->V[chip8->inst.X] & 0x80) >> 7;

                    chip8->V[chip8->inst.X] <<= 1;
                    break;

                default:
                    // UNIMPLEMENTED
                    break;
            }
            break;

        case 0x09:
            // 0x9XY0: Skips next instruction if VX != VY
            if (chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y]) chip8->PC += 2;   // Increment past current (next) opcode, skipping the instruction
            break;

        case 0x0A:
            // 0xANNN: Set index register I = NNN
            chip8->I = chip8->inst.NNN;
            break;

        case 0x0B:
            // 0xBNNN: Set PC to V0 + NNN, jumps to address NNN + V0
            chip8->PC = chip8->V[0] + chip8->inst.NNN;
            break;

        case 0x0C:
            // 0xCXNN: Set VX to rand() 0-255 inclusive bitwise ANDed with NN
            chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
            break;

        case 0x0D: {
            // 0xDXYN: Draw/display N pixel high sprite from memory at index register I, at screen coordinates
            //   VX and VY
            // Get starting X/Y coord values from VX and VY, wrapping as needed
            uint8_t X = chip8->V[chip8->inst.X] & (CHIP8_XRES-1);
            uint8_t Y = chip8->V[chip8->inst.Y] & (CHIP8_YRES-1);
            uint8_t orig_X = X; // Save original X value

            // Initialize carry flag to 0
            chip8->V[0xF] = 0;

            // Drawing N rows of the sprite
            for (uint8_t i = 0; i < chip8->inst.N; i++) {
                // Get ith byte/row of sprite from memory at index register I
                uint8_t sprite_row = chip8->ram[chip8->I + i];

                X = orig_X;     // Reset X for new row

                // Check each bit in this byte/row, xor it with display pixel,
                //   if any are on, set the carry flag VF
                for (int8_t j = 7; j >= 0; j--) {
                    if (sprite_row & (1 << j)) {
                        // Bit is 1 or "on"
                        if (chip8->display[Y * CHIP8_XRES + X]) {
                            // Display pixel is also on
                            chip8->V[0xF] = 1;                          // Set carry flag, "collision detection"
                            chip8->display[Y * CHIP8_XRES + X] = false; // Turn off display pixel
                        } else {
                            // Display pixel is not on
                            chip8->display[Y * CHIP8_XRES + X] = true; // Turn on display pixel
                        }
                    }

                    // Increment X coordinate, stop drawing if hit right edge of screen
                    if (++X >= CHIP8_XRES) 
                        break;
                }

                // Increment Y coordinate, stop drawing if hit bottom edge of screen
                if (++Y >= CHIP8_YRES)  
                    break;
            }

            chip8->draw_flag = true;    // Will draw changes at next 60hz display tick
        }
        break;

        case 0x0E:
            switch (chip8->inst.NN) {
                case 0x9E:
                    // 0xEX9E: Skips the next intruction if the key in VX is pressed (the data in VX will be 0x0-0xF)
                    if (chip8->keypad[chip8->V[chip8->inst.X]] == true) 
                        chip8->PC += 2; // Increment past current (next) opcode, skipping the instruction
                    break;

                case 0xA1:
                    // 0xEXA1: Skips the next intruction if the key in VX is not pressed (the data in VX will be 0x0-0xF)
                    if (chip8->keypad[chip8->V[chip8->inst.X]] == false) 
                        chip8->PC += 2; // Increment past current (next) opcode, skipping the instruction
                    break;

                default:
                    break;
            }
            break;

        case 0x0F:
            switch (chip8->inst.NN) {
                case 0x07:
                    // FX07: Set VX to value of delay timer
                    chip8->V[chip8->inst.X] = chip8->delay_timer;
                    break;

                case 0x0A:
                    // FX0A: Await keypress, store in VX when pressed
                    // Check if any keys are pressed
                    bool any_pressed = false;
                    for (uint8_t i = 0; i < sizeof(chip8->keypad); i++) {
                        if (chip8->keypad[i] == true) {
                            chip8->V[chip8->inst.X] = i;    // Store key value (index) in VX
                            any_pressed = true;
                            break;
                        }
                    }

                    // If no keys are pressed, decrement the program counter so that the next instruction is this one again,
                    //   effectively "blocking" any other instructions or the program from continuing until a key is pressed
                    if (!any_pressed) chip8->PC -= 2;  
                    break;

                case 0x15:
                    // FX15: Set delay timer to VX value
                    chip8->delay_timer = chip8->V[chip8->inst.X];
                    break;

                case 0x18:
                    // FX18: Set sound timer to VX value
                    chip8->sound_timer = chip8->V[chip8->inst.X];
                    break;

                case 0x1E:
                    // FX1E: Add VX to I, I += VX
                    chip8->I += chip8->V[chip8->inst.X];
                    break;

                case 0x29:
                    // FX29: Set index register I to location of sprite in VX
                    // Each font character 0x0-0xF is 5 bytes in RAM, so can
                    //   offset into font by VX*5
                    // For this emulator, the font is stored at the beginning of RAM,
                    //  0x0-0x50 (80 bytes)
                    chip8->I = chip8->V[chip8->inst.X] * 5;
                    break;

                case 0x33:
                    // FX33: Store BCD representation of data in VX at I (hundreds), I+1 (tens), and I+2 (ones)
                    // e.g. VX = 123, I+2 = 3, I+1 = 2, I = 1
                    uint8_t bcd = chip8->V[chip8->inst.X];
                    chip8->ram[chip8->I+2] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I+1] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I] = bcd;
                    break;

                case 0x55:
                    // FX55: Store registers V0-VX inclusive in memory, starting from index register I
                    //   I is not incremented, but is offset from
                    for (uint8_t i = 0; i <= chip8->inst.X; i++) 
                        chip8->ram[chip8->I + i] = chip8->V[i];
                    break;

                case 0x65:
                    // FX65: Load registers V0-VX inclusive from memory, starting from index register I
                    //   I is not incremented, but is offset from
                    for (uint8_t i = 0; i <= chip8->inst.X; i++) 
                        chip8->V[i] = chip8->ram[chip8->I + i];
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

// If 1/60th of a second has passed, increment timers & update display
void update_timers_display(chip8_t *chip8, const config_t config) {    
    if (chip8->delay_timer > 0) chip8->delay_timer--;

    if (chip8->sound_timer > 0) {
        // Play sound
        SDL_PauseAudioDevice(dev, 0);
        chip8->sound_timer--;
    } else {
        // Stop sound
        SDL_PauseAudioDevice(dev, 1);
    }

    // Show display changes to the user
    if (chip8->draw_flag) {
        chip8->draw_flag = false;
        draw_display(*chip8, config);
    }
}

// Da main squeeze
int main(int argc, char *argv[]) {
    // Rom name to load and emulate
    char rom_name[256] = {0};
    uint64_t start_16ms_time, end_16ms_time, diff_16ms;  // SDL ms counters 

    // Amount to scale the display by, and any pixels by.
    // This will scale the original CHIP8 resolution and original CHIP8 "pixel" size
    //   e.g. 1px at original 64x32 resolution with scale_factor = 20 => 1px at 20px new size
    config.scale_factor = 20;

    config.foreground_color = 0xFFFFFFFF;   // 8 bit RGBA
    config.background_color = 0x00000000;   // 8 bit RGBA
    config.insts_per_second = 700;          // # of CHIP8 instructions to emulate in 1 second
    config.draw_pixel_outlines = true;      // Draw pixel outlines yes or no
    config.square_wave_freq = 1244;         // Note frequency to play (D#6)

    // Get rom name
    if (argc < 2) {
        // No command line arg for ROM name, use a default rom
        strncpy(rom_name, "../chip8-roms/programs/IBM Logo.ch8", sizeof(rom_name));
    } else {
        strncpy(rom_name, argv[1], strlen(argv[1])); // 1st arg after program name
    }

    // Initialize SDL
    init_SDL(config.scale_factor);

    // Set up program cleanup at exit
    atexit(cleanup);

    // Initialize new chip8 machine
    chip8_t chip8 = init_chip8(rom_name); 

    // Set up initial scene (background color, etc.)
    clear_screen(config);

    // Set up (seed) random number generator
    srand(time(NULL));

    // Emulator loop    
    while (true) {
        // Handle user input
        handle_input(&chip8);

        // If currently paused, do not go on, keep busy looping until not paused
        if (chip8.paused) continue;

        // Get current frame start time 
        start_16ms_time = SDL_GetPerformanceCounter(); 

        // Run number of instructions per frame (60fps)
        for (uint32_t i = 0; i < config.insts_per_second / 60; i++) 
            emulate_instruction(&chip8);

        // Get current frame time elapsed
        end_16ms_time = SDL_GetPerformanceCounter();    

        // Delay until full frame time
        diff_16ms = (double)((end_16ms_time - start_16ms_time) * 1000) / SDL_GetPerformanceFrequency();

        if (diff_16ms < 16.67f) SDL_Delay(16.67f - diff_16ms);  

        // Update delay/sound timers and display every frame
        update_timers_display(&chip8, config); 
    }

    // Clean up heap allocations
    free(&chip8);

    return EXIT_SUCCESS;
}











