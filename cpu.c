#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread/pthread.h>

#define true  1U;
#define false 0U;
typedef unsigned char  uint8;
typedef unsigned short uint16;
typedef unsigned int   uint32;
typedef uint8 BOOL;


static uint8  memory[0x10000];
static uint8  regArray[0x10];
static uint16 regPC;
static uint16 regI;

static BOOL     runCycle = false;
static uint32   masterClockTimer = 0;
static BOOL     runBinary = true;

void *masterClock(void *vargp);
static void executeCycle(void);
static void decode(uint16  inst);
static void subOp8(uint16 inst);
static void subOpF(uint16 inst);

int main()
{
    
    long romSize = 0;
    pthread_t timerThreadID;
    //start program counter at 0x200
    regPC = 0x0200U;

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

    //create Master clock thread at 500Hz
    pthread_create(&timerThreadID, NULL, &masterClock, NULL);

    //Main Loop
    while(runBinary){
        if(runCycle)
        {
            //run current cycle
            executeCycle();
            runCycle = false;
        }
    }
    pthread_join(timerThreadID, NULL);
    return 0;
}

void * masterClock(void *vargp)
{   
    #define MSEC_2 2000         //2 * ( CLOCKS_PER_SEC / 1000 )
    clock_t baselineTime;

    baselineTime = clock();

    //Clock timer increments at a rate of 500Hz (2ms)
    while(runBinary)
    {
        if((baselineTime - clock()) >= MSEC_2)
        {
            masterClockTimer++;
            runCycle = true;
            baselineTime = clock();
        }
    }
    return NULL;
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
    uint16 addr;
    uint8 op1 = (uint8)((0xF000U & inst) >> 12);
    regX = (0x0F00 & inst) >> 8;
    regY = (0x00F0 & inst) >> 4;
    literal = (uint8)(0x00FF & inst);
    addr = 0x0FFF & inst;
    //TO-DO check bounds on regx and y

    switch(op1)
    {
        case 0x00E0:
        {
            break;
        }
        case 0x00EE:
        {
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

            break;
        }
        case 0xD:
        {
            //DRAW
            break;
        }
        case 0xE:
        {
            //SKP
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

}