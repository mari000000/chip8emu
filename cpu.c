#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <pthread/pthread.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_keyboard.h>

#define true  1U
#define false 0U
typedef unsigned char  uint8;
typedef signed char    sing8;
typedef unsigned short uint16;
typedef signed short   sint16;
typedef unsigned int   uint32;
typedef signed int     sint32;
typedef uint8 BOOL;

const int AMPLITUDE = 28000;
const int SAMPLE_RATE = 44100;

static uint8  memory[0x10000];
static uint8  regArray[0x10];
static uint16 regPC;
static uint16 regI;
static uint16 stack[17] = {0}; //16+1 for direct indexing because idk how the roms use this
static uint16 sp = 0;
static uint8  regDelay;
static uint8  regSound;

static uint8  screen[64*32];

static BOOL   keypad[16];

static BOOL     runCycle = false;
static uint32   masterClockTimer = 0;
static BOOL     runBinary = true;



//counters
uint32 i;
uint32 j;

void setPixel(SDL_Surface * surface, uint32 x, uint32 y, uint32 pixel);
void audio_callback(void *user_data, uint8 *raw_buffer, int bytes);
void *masterClock(void *vargp);
static void load_chip8_fontset(void);
static void executeCycle(void);
static void decode(uint16 inst);
static void subOp8(uint16 inst);
static void subOpF(uint16 inst);


int main()
{
    SDL_Window * window = NULL;
    SDL_Surface * screenSurface = NULL;
    const uint8 * keyboard = NULL;
    SDL_Event event;
    
    

    //Temp remove this
    // for( i = 0; i < sizeof(screen); i+=3)
    // {
    //     screen[i] = 0x01;
    // }

    if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO) != 0)
    {
        SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("Space Invaders", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 320, SDL_WINDOW_SHOWN);
    if(window == NULL)
    {
        SDL_Log("Failed to allocate screen memory: %s", SDL_GetError());
    }
    else
    {
        //Get window surface
        screenSurface = SDL_GetWindowSurface( window );

        //Fill the surface white
        //SDL_FillRect( screenSurface, NULL, SDL_MapRGB( screenSurface->format, 0xFF, 0xFF, 0xFF ) );
        
        //SDL_UpdateWindowSurface(window);
    }
    long romSize = 0;
    pthread_t timerThreadID;
    //start program counter at 0x200
    regPC = 0x0200U;

    srand(time(0));

    //open rom to run
    FILE * rom;
    rom = fopen("./testRoms/SpaceInvaders.ch8", "r");

    if(rom == NULL)
    {
        runBinary = false;
    }
    else
    {
        //get file size
        fseek(rom, 0L, SEEK_END);
        romSize = ftell(rom);
        rewind(rom);

        //copy rom into ram
        fread(&memory[regPC], romSize, 1, rom);

        //close file, we don't need it anymore
        fclose(rom);
    }

    load_chip8_fontset();

    //create Master clock thread at 500Hz
    pthread_create(&timerThreadID, NULL, &masterClock, NULL);

    //Main Loop
    while(runBinary){
        if(runCycle)
        {
            //Get Keyboard state
            SDL_PollEvent(&event);//dont check this just yet
            keyboard = SDL_GetKeyboardState(NULL);
            if(keyboard[SDL_SCANCODE_Q])
            {
                runBinary = false;
                break;
            }

            //run current cycle
            executeCycle();

            //Update screen surface
            SDL_LockSurface(screenSurface);

            for(i = 0; i < 64; i++)
            {
                for(j = 0; j < 32; j++)
                {
                    if(screen[i + (j * 64)] == 1)
                    {
                        setPixel(screenSurface, i, j, 0x0000FF00);
                    }
                    else
                    {
                        setPixel(screenSurface, i, j, 0xFF000000);
                    }
                }
            }
            SDL_UnlockSurface(screenSurface);

            //write to window
            SDL_UpdateWindowSurface(window);
            
            runCycle = false;
        }
    }

    //clean up
    pthread_join(timerThreadID, NULL);
    SDL_DestroyWindow(window);
    
    SDL_Quit();
    return 0;
}

void setPixel(SDL_Surface * surface, uint32 x, uint32 y, uint32 pixel)
{
    //scale and set pixel
    //fixed 10*10 size
    uint32 actualX = x * 10;
    uint32 actualY = y * 10;
    uint32 i;
    uint32 j;
    for( i = actualX; i < (actualX + 10); i++)
    {
        for( j = actualY; j < (actualY + 10); j++)
        {
            uint8 * targetPixel = (uint8 *)surface->pixels + j * surface->pitch + i * 4;
            *(uint32 *)targetPixel = pixel;
        }
    }
    
}

void audio_callback(void *user_data, uint8 *raw_buffer, int bytes)
{
    sint16 *buffer = (sint16*)raw_buffer;
    int length = bytes / 2; // 2 bytes per sample for AUDIO_S16SYS
    int sample_nr = *(int*)user_data;

    for(int i = 0; i < length; i++, sample_nr++)
    {
        double time = (double)sample_nr / (double)SAMPLE_RATE;
        buffer[i] = (Sint16)(AMPLITUDE * sin(2.0f * M_PI * 441.0f * time)); // render 441 HZ sine wave
    }
}

void * masterClock(void *vargp)
{   
    #define MSEC_2      2000         //2 * ( CLOCKS_PER_SEC / 1000 )
    #define MSEC_16P6   16667        //16.667 * ( CLOCKS_PER_SEC / 1000 )
    clock_t baselineTime;
    clock_t timerClk60;
    int sample_nr = 0;
    //SDL_AudioSpec want;
    //SDL_AudioSpec have;
    BOOL    soundPlaying = false;

    baselineTime = clock();
    timerClk60 = baselineTime;

    //want.freq = SAMPLE_RATE;
    //want.format = AUDIO_S16SYS;
    // want.channels = 1;
    // want.samples = 2048;
    // want.callback = audio_callback;
    // want.userdata = &sample_nr;

    //if(SDL_OpenAudio(&want, &have) != 0) SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to open audio: %s", SDL_GetError());
    //if(want.format != have.format) SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to get the desired AudioSpec");


    //Clock timer increments at a rate of 500Hz (2ms)
    while(runBinary)
    {
        if((baselineTime - clock()) >= MSEC_2)
        {
            masterClockTimer++;
            runCycle = true;
            baselineTime = clock();
        }

        // 60Hz Timers
        if((baselineTime - clock()) >= MSEC_16P6)
        {
            if(regDelay != 0U)
            {
                regDelay--;
            }
            if(regSound != 0U)
            {
                regSound--;
                if(soundPlaying == false)
                {
                    //play sound
                    //SDL_PauseAudio(0); // start playing sound
                }
            }
            else 
            {
                //stop sound
                //SDL_PauseAudio(1); // stop playing sound
            }
        }
    }
    //SDL_CloseAudio(); //audio causing exception on quit
    return NULL;
}

static void load_chip8_fontset(void)
{
    uint8 chip8_fontset[80] =
        { 
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
    
    memcpy(&memory[0x50], &chip8_fontset[0], sizeof(chip8_fontset));
}

static void executeCycle(void)
{
    uint16 instruction = 0U;

    instruction = memory[regPC] << 8;
    instruction |= memory[regPC + 1];
    decode(instruction);

    //increment program counter
    regPC = regPC + 2;
}

static void decode(uint16 inst)
{
    //parse op code
    uint8 literal;
    uint8 regX;
    uint8 regY;
    uint8 n;
    uint16 addr;
    uint8 op1 = (uint8)((0xF000U & inst) >> 12);
    regX = (0x0F00 & inst) >> 8;
    regY = (0x00F0 & inst) >> 4;
    n = (0x000F & inst);
    literal = (uint8)(0x00FF & inst);
    addr = 0x0FFF & inst;
    //TO-DO check bounds on regx and y

    switch(op1)
    {
        case 0x00E0:
        {
            //CLS
            //clear screen
            memset(&screen[0], 0, sizeof(screen));
            break;
        }
        case 0x00EE:
        {
            //RETURN
            regPC = stack[sp];
            sp = sp - 1;
            regPC = regPC - 2; //subtract 2 because 2 will be added at end of cycle
            break;
        }
        case 0x1:
        {
            //JMP
            regPC = addr - 2; //subtract 2 because 2 will be added at end of cycle
            break;
        }
        case 0x2:
        {
            //CALL
            sp = sp + 1;
            stack[sp] = regPC;
            regPC = addr - 2; //subtract 2 because 2 will be added at end of cycle
            break;
        }
        case 0x3:
        {
            //SE
            //Vx == kk
            if(regArray[regX] == literal)
            {
                regPC = regPC + 2;
            }
            break;
        }
        case 0x4:
        {
            //SNE
            //Vx != kk
            if(regArray[regX] != literal)
            {
                regPC = regPC + 2;
            }
            break;
        }
        case 0x5:
        {
            //SE
            //Vx == Vy
            if(regArray[regX] == regArray[regY])
            {
                regPC = regPC + 2;
            }
            break;
        }
        case 0x6:
        {
            //LD
            //Vx = kk
            regArray[regX] = literal;
            break;
        }
        case 0x7:
        {
            //ADD
            //Vx = Vx + kk
            regArray[regX] = regArray[regX] + literal;
            break;
        }
        case 0x8:
        {
            subOp8(inst);
            break;
        }
        case 0x9:
        {
            //SNE
            //Vx != Vy
            if(regArray[regX] != regArray[regY])
            {
                //increment here; at end of excecution, it will be incremented again
                regPC = regPC + 2;
            }
            break;
        }
        case 0xA:
        {
            //LD I
            regI = addr;
            break;
        }
        case 0xB:
        {
            //JMP
            //pc = addr + v0
            regPC = addr + regArray[0x0] - 2; //subtract 2 because 2 will be added at end of cycle
            break;
        }
        case 0xC:
        {
            //RND
            regArray[regX] = (literal & (uint8)rand());
            break;
        }
        case 0xD:
        {
            //DRAW
            uint8 i;
            uint8 j;
            uint8 byte;
            uint16 position;
            uint8 bit;

            /*Draws a sprite at coordinate (VX, VY) that has a width of 8 pixels and a height of N pixels. 
            Each row of 8 pixels is read as bit-coded starting from memory location I; 
            I value doesn’t change after the execution of this instruction. As described above, 
            VF is set to 1 if any screen pixels are flipped from set to unset when the sprite is drawn, 
            and to 0 if that doesn’t happen */
            regArray[0xF] = 0;

            for( j = 0; j < n; j++ )
            {
                byte = memory[regI + j];
                for( i = 0; i < 8; i++ )
                {
                    position = ((regX + j )* 64) + regY + i;
                    bit = (byte >> (7 - i)) & 1;
                    if(screen[position] == 1 && bit == 0)
                    {
                        regArray[0xF] = 1;
                    }
                    screen[position] = bit; 
                }
            }
            
            break;
        }
        case 0xE:
        {
            //SKP
            switch (literal)
            {
                case 0x9E:
                    if(keypad[regArray[regX]] == true)
                    {
                        regPC = regPC + 2;
                    }
                    break;

                case 0xA1:
                    if(keypad[regArray[regX]] == false)
                    {
                        regPC = regPC + 2;
                    }
                    break;
            }
            break;
        }
        case 0xF:
        {
            subOpF(inst);
            break;
        }
        default:
        {
            break;
        }
    } 
}

static void subOp8(uint16 inst)
{
    uint8 op2;
    uint8 regX;
    uint8 regY;
    uint16 overflow;

    op2 = 0xF & inst;
    regX = (0x0F00 & inst) >> 8;
    regY = (0x00F0 & inst) >> 4;
    //TO-DO check bounds on regx and y

    switch(op2)
    {
        case 0x0:
            //assignment
            //Vx=Vy
            regArray[regX] = regArray[regY];
            break;

        case 0x1:
            //OR
            //Vx=Vx|Vy
            regArray[regX] |= regArray[regY];
            break;

        case 0x2:
            //AND
            //Vx=Vx&Vy
            regArray[regX] &= regArray[regY];
            break;

        case 0x3:
            //XOR
            //Vx=Vx^Vy
            regArray[regX] ^= regArray[regY];
            break;

        case 0x4:
            //ADD
            //VX += Vy
            overflow = regArray[regX];
            overflow += regArray[regY];
            if(overflow > 255)
            {
                //set carry
                regArray[0xF] = 1;
                regArray[regX] = 0x00FF & overflow;
            }
            else
            {
                //clear carry
                regArray[0xF] = 0;
                regArray[regX] = 0x00FF & overflow;
            }
            break;

        case 0x5:
            //SUB
            //Vx=Vx-Vy
            if(regArray[regX] > regArray[regY])
            {
                regArray[0xF] = 1;
            }
            else
            {
                regArray[0xF] = 0;
            }
            regArray[regX] = regArray[regX] - regArray[regY];
            break;

        case 0x6:
            //SHR
            //Vx = Vx >> 1
            regArray[0xF] = (regArray[regX] & 1U) ? 1 : 0;
            regArray[regX] = regArray[regX] >> 1;
            break;

        case 0x7:
            //SUBN
            //Vx = Vy-Vx
            regArray[0xF] = (regArray[regY] > regArray[regX]) ? 1 : 0;
            regArray[regX] = regArray[regY] - regArray[regX];
            break;

        case 0xE:
            //SHL
            //Vx = Vx << 1
            regArray[0xF] = (regArray[regX] & 0x80) ? 1 : 0;
            regArray[regX] = regArray[regX] << 1;
            break;

        default:
            break;
    }
}

static void subOpF(uint16 inst)
{
    uint8 op2;
    uint8 regX;
    uint8 regCount;

    op2 = 0xFFU & inst;
    regX = (0x0F00 & inst) >> 8;
    //TO-DO check bounds on regx

    switch(op2)
    {
        case 0x07:
            //LD Delay
            regArray[regX] = regDelay;
            break;

        case 0x0A:
            //Wait for key press
            break;

        case 0x15:
            //SET Delay
            regDelay = regArray[regX];
            break;

        case 0x18:
            //SET Sound
            regSound = regArray[regX];
            break;

        case 0x1E:
            //ADD I
            regI = regI + regArray[regX];
            break;

        case 0x29:
            regI = memory[ 0x50 + (regX * 5) ];
            break;

        case 0x33:
            memory[regI]     = regArray[regX] / 100;
            memory[regI + 1] = (regArray[regX] / 10) % 10;
            memory[regI + 2] = (regArray[regX] % 100) % 10;
            break;

        case 0x55:
            //STORE [I]
            for(regCount = 0; regCount <= regX; regCount++)
            {
                memory[regI + regCount] = regArray[regCount];
            }
            break;

        case 0x65:
            //LOAD [I]
            for(regCount = 0; regCount <= regX; regCount++)
            {
                regArray[regCount] = memory[regI + regCount];
            }
            break;

        default:
            break;
    }

}