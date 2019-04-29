#include <iostream>
#include <fstream>
#include <SDL2/SDL.h>
#include <chrono>
#include <thread>
#include <signal.h>

#define FLAG_REG 0xF
#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 32

struct Regs {
    uint8_t V[16];
    uint16_t I; //memory addresses, only 12 bits used since memory 0-4095

    uint16_t pc;
    uint8_t sp;
};

struct Regs registers;
uint8_t memory[4096];
uint16_t stack[16];

uint8_t screen[SCREEN_WIDTH * SCREEN_HEIGHT];
bool drawFlag;

void big_to_small_endian(char *buf, int size) {
    uint8_t buf2[size];
    for(int x = 0; x < size; x++) {
        buf2[x] = buf[x];
    }

    for(int x = 0; x < size; x++) {
        buf[x] = buf2[(size - 1) - x];
    }
}

void loadProgram() {
    FILE *file = fopen("Airplane.ch8", "rb");

    int block_size = 1024;
    int index = 0x200;
    int read_amount = block_size;
    while(read_amount == block_size) {
        read_amount = fread(&memory[index], block_size, 1, file);
        index += block_size;
    }
    fclose(file);
}

/*uint16_t pop16() {
    uint16_t val = stack[registers.sp] << 8 | stack[registers.sp + 1];
    registers.sp += 2;
}*/
void run_iteration() {
    //fetch
    uint16_t opcode = (memory[registers.pc] << 8) | memory[registers.pc + 1];
    registers.pc += 2;
    //decode & execute
    switch(opcode & 0xF000) {
        case 0x0000:
        {
            if(opcode == 0x00E0) {
                //clear screen
            } else if(opcode == 0x00EE) {
                //return from subroutine
                registers.pc = stack[registers.sp];
                registers.sp -= 1;
            }
            break;
        }
        case 0x1000: //Jump to 0XXX
        {
            registers.pc = opcode & 0x0FFF;
            break;
        }
        case 0x2000: //Call 0XXX
        {
            registers.sp + 1;
            stack[registers.sp] = registers.pc;
            registers.pc = opcode & 0x0FFF;
            break;
        }
        case 0x3000: //3xkk, skip next instruction if V[x] == kk
        {
            uint8_t x = opcode & 0x0F00 >> 8;
            uint8_t k = opcode & 0xFF;
            if(registers.V[x] == k) {
                registers.pc += 2;
            }
            break;
        }
        case 0x4000: //4xkk, skip next instruction if V[x] != kk
        {
            uint8_t x = opcode & 0x0F00 >> 8;
            uint8_t k = opcode & 0xFF;
            if(registers.V[x] != k) {
                registers.pc += 2;
            }
            break;
        }
        case 0x5000: //5xy0 skip next instruction if Vx == Vy
        {
            uint8_t x = opcode & 0x0F00 >> 8;
            uint8_t y = opcode & 0xF >> 4;
            if(registers.V[x] == y) {
                registers.pc += 2;
            }
            break;
        }
        case 0x6000: //6xkk LD Vx, byte
        {
            uint8_t x = opcode & 0x0F00 >> 8;
            uint8_t k = opcode & 0xFF;
            registers.V[x] = k;
            break;
        }
        case 0x7000: //7xkk Vx = Vx + kk
        {
            uint8_t x = opcode & 0x0F00 >> 8;
            uint8_t k = opcode & 0xFF;

            registers.V[x] = registers.V[x] + k;
            break;
        }
        case 0x8000:
        {
                uint8_t x = opcode & 0x0F00 >> 8;
                uint8_t y = opcode & 0xF0 >> 4;
            switch(opcode & 0xF) {
                case 0x0: //8xy0 LD Vx, Vy
                registers.V[x] = registers.V[y];
                break;
                case 0x1:
                registers.V[x] |= registers.V[y];
                break;
                case 0x2:
                registers.V[x] |= registers.V[y];
                break;
                case 0x3:
                registers.V[x] ^= registers.V[y];
                break;
                case 0x4:
                if((0xFF - registers.V[x]) < registers.V[y]) {
                    registers.V[FLAG_REG] = 1;
                } else {
                    registers.V[FLAG_REG] = 0;
                }
                registers.V[x] += registers.V[y];
                break;
                case 0x5:
                registers.V[FLAG_REG] = registers.V[x] > registers.V[y];
                registers.V[x] -= registers.V[y];
                break;
                case 0x6:
                registers.V[FLAG_REG] = registers.V[x] & 0x1;
                registers.V[x] >>= 1;
                break;
                case 0x7:
                registers.V[FLAG_REG] = registers.V[y] > registers.V[x];
                registers.V[x] -= registers.V[y];
                break;
                case 0xE:
                registers.V[FLAG_REG] = registers.V[x] & 0x80 >> 7;
                registers.V[x] <<= 1;
                break;
            }
            break;
        }
        case 0x9000:
        {
            uint8_t x = opcode & 0x0F00 >> 8;
            uint8_t y = opcode & 0xF0 >> 4;
            if(registers.V[x] != registers.V[y]) {
                registers.pc += 2;
            }
            break;
        }
        case 0xA000:
        {
            registers.I = opcode & 0x0FFF;
        }
        break;
        case 0xB000:
        {
            uint16_t jmpLoc = opcode & 0x0FFF;
            registers.pc = jmpLoc + registers.V[0];
            break;
        }
        //case 0xC000:
        //break;
        case 0xD000:
        {
            uint8_t origX = 0x0F00 >> 8;
            uint8_t origY = 0x00F0 >> 4;
            uint8_t n = 0xF;
            int SPRITES_WIDTH = 8;

            registers.V[FLAG_REG] = 0;

            for(int memIndex = registers.I; memIndex < registers.I + n; memIndex += 1) {
                int x = origX + memIndex % SPRITES_WIDTH;
                int y = origY + memIndex / SPRITES_WIDTH;

                x %= SCREEN_WIDTH;
                y %= SCREEN_HEIGHT;

                if(screen[x + y * SCREEN_WIDTH] == 1 && memory[memIndex] == 1) {
                    registers.V[FLAG_REG] = 1;   
                }
                screen[x + y * SCREEN_WIDTH] ^= memory[memIndex];
            }
        break;
        }
        default:
        {
            printf("Unhandled opcode 0x%X\n", opcode);
            break;
        }
    }

}
void updateScreen(SDL_Renderer *renderer, SDL_Texture *sdlTexture) {
    uint32_t pixels[2048];
    for(int x = 0 ; x < 2048; x++) {
        pixels[x] = (0x00FFFFFF * screen[x]) | 0xFF000000;
    }
    SDL_UpdateTexture(sdlTexture, NULL, pixels, 64 * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, sdlTexture, NULL, NULL);
    SDL_RenderPresent(renderer);
}
void stop(int signum) {
    exit(0);
}
int main() {
    loadProgram();
    registers.pc = 0x200;
    signal(SIGINT, stop);

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow("Chip-8", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    SDL_Texture *sdlTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 64, 32);
    

    while(true) {
        run_iteration();
        updateScreen(renderer, sdlTexture);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}