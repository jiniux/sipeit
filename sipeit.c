#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>

#define SIPEIT_PROGRAM_OFFSET 512
#define SIPEIT_TOTAL_MEMORY 4096

#define SIPEIT_DISPLAY_WIDTH  64
#define SIPEIT_DISPLAY_HEIGHT 32

// IO

uint16_t keys[16]                       = { 0 };

// Timers

#define SIPEIT_TIMERS_HERTZ    600
#define SIPEIT_TIMERS_INTERVAL ((double)1 / (double)SIPEIT_TIMERS_HERTZ)

uint8_t delay_timer = 0;
uint8_t sound_timer = 0;

// Memory

uint8_t  memory[SIPEIT_TOTAL_MEMORY * 2]    = { 0 };
uint16_t stack[16]                      = { 0 };

// GFX

uint32_t vmemory[SIPEIT_DISPLAY_HEIGHT][SIPEIT_DISPLAY_WIDTH] = { 0 };

// CPU

#define SIPEIT_OPCODE_CALL_NATIVE   0x0
#define SIPEIT_OPCODE_JUMP          0x1
#define SIPEIT_OPCODE_CALL          0x2
#define SIPEIT_OPCODE_EQ_CONST      0x3
#define SIPEIT_OPCODE_NEQ_CONST     0x4
#define SIPEIT_OPCODE_EQ_REG        0x5
#define SIPEIT_OPCODE_ASSIGN_CONST  0x6
#define SIPEIT_OPCODE_ADD_CONST     0x7
#define SIPEIT_OPCODE_ARITHMETIC    0x8
#define SIPEIT_OPCODE_NEQ_REG       0x9
#define SIPEIT_OPCODE_ASSIGN_I      0xA
#define SIPEIT_OPCODE_RELATIVE_JUMP 0xB
#define SIPEIT_OPCODE_RAND          0xC
#define SIPEIT_OPCODE_DRAW          0xD
#define SIPEIT_OPCODE_KEY_PRESSED   0xE
#define SIPEIT_OPCODE_MISC          0xF

#define NEXT_INSTR()        (instruction_pointer + 2)
#define SKIP_INSTR_IF(...)  (instruction_pointer += (2 * (__VA_ARGS__)))

#define INC_INSTR_POINTER() (instruction_pointer += 2)

#define NIBBLE(DATA, INDEX)    ((DATA >> INDEX * 4) & (0x000F))
#define BYTE(DATA, INDEX)      ((DATA >> INDEX * 8) & (0x00FF))
#define TRIBBLE(DATA, INDEX)   ((DATA >> INDEX * 12) & (0x0FFF))

#define SIPEIT_ERROR_MISSING_ARGUMENT     -1
#define SIPEIT_ERROR_FILE                 -2
#define SIPEIT_ERROR_OOB_NATIVE_CALL      -3
#define SIPEIT_ERROR_INVALID_INSTRUCTION  -4

#define CRASH(STATUS, MSG) ({ fprintf(stderr, "ERROR: " MSG "\n"); exit(STATUS); })
#define CRASHFMT(STATUS, MSG, ...) ({ fprintf(stderr, "ERROR: " MSG "\n", __VA_ARGS__); exit(STATUS); })

#define CRASH_INVALID_INSTRUCTION()\
    CRASHFMT(\
        SIPEIT_ERROR_INVALID_INSTRUCTION,\
        "Invalid instruction (%#04X) at %d. [%d]",\
        instruction,\
        __LINE__,\
        instruction_pointer - SIPEIT_PROGRAM_OFFSET)

uint16_t instruction_pointer = SIPEIT_PROGRAM_OFFSET;

uint8_t  stack_index      = 0;
uint8_t  v_registers[16]  = {0};
uint16_t address_register = 0;

static void stack_push(uint16_t address)
{
    stack[stack_index++] = address;
}

static uint16_t stack_pop()
{
    return stack[--stack_index];
}

static void display_clear()
{
    memset(vmemory, 0, sizeof(vmemory));
}

static void return_from_routine()
{
    instruction_pointer = stack_pop();
}

#define SIPEIT_MISC_GET_DELAY   0x07
#define SIPEIT_MISC_SET_DT      0x15
#define SIPEIT_MISC_SET_ST      0x18
#define SIPEIT_MISC_WAIT_KEY    0x0A
#define SIPEIT_MISC_ADD_I       0x1E
#define SIPEIT_MISC_SET_SPRITE  0x29
#define SIPEIT_MISC_BCD         0x33
#define SIPEIT_MISC_REG_DUMP    0x55
#define SIPEIT_MISC_REG_LOAD    0x65

#define SIPEIT_ARITHMETIC_ASSIGN 0x0
#define SIPEIT_ARITHMETIC_OR     0x1
#define SIPEIT_ARITHMETIC_AND    0x2
#define SIPEIT_ARITHMETIC_XOR    0x3
#define SIPEIT_ARITHMETIC_ADD    0x4
#define SIPEIT_ARITHMETIC_SUB    0x5
#define SIPEIT_ARITHMETIC_SHIFTR 0x6
#define SIPEIT_ARITHMETIC_RSUB   0x7
#define SIPEIT_ARITHMETIC_SHIFTL 0xE

bool waiting_for_key = false;
uint8_t v_register_key_index = 0;

inline static void cpu_step()
{

    if (waiting_for_key)
        return;

    uint16_t instruction = memory[instruction_pointer] << 8 | memory[instruction_pointer+1];

    if (instruction == 0x0000)
        return;

    uint8_t opcode = (instruction & 0xF000) >> 12;

    switch (opcode)
    {
    case SIPEIT_OPCODE_CALL_NATIVE:
    {
        uint16_t interrupt = TRIBBLE(instruction, 0);

        switch (interrupt)
        {
        case 0x00E0:
            display_clear();
            break;
        
        case 0x00EE:
            return_from_routine();
            return;

        default:
            CRASH_INVALID_INSTRUCTION();
            break;
        }
    }
    break;

    case SIPEIT_OPCODE_JUMP:
        instruction_pointer = TRIBBLE(instruction, 0);
        return;

    case SIPEIT_OPCODE_CALL:
        stack_push(NEXT_INSTR());
        instruction_pointer = TRIBBLE(instruction, 0);
        return;

    case SIPEIT_OPCODE_EQ_CONST:
    {
        const uint8_t value = BYTE(instruction, 0);
        const uint8_t vx = v_registers[NIBBLE(instruction, 2)];

        SKIP_INSTR_IF(value == vx);
    }
    break;

    case SIPEIT_OPCODE_NEQ_CONST:
    {
        const uint8_t value = BYTE(instruction, 0);
        const uint8_t vx = v_registers[NIBBLE(instruction, 2)];

        SKIP_INSTR_IF(value != vx);
    }
    break;

    case SIPEIT_OPCODE_NEQ_REG:
    {
        const uint8_t vx = v_registers[NIBBLE(instruction, 2)];
        const uint8_t vy = v_registers[NIBBLE(instruction, 1)];

        SKIP_INSTR_IF(vx != vy);
    }
    break;

    case SIPEIT_OPCODE_EQ_REG:
    {
        const uint8_t vx = v_registers[NIBBLE(instruction, 2)];
        const uint8_t vy = v_registers[NIBBLE(instruction, 1)];

        SKIP_INSTR_IF(vx == vy);
    }
    break;

    case SIPEIT_OPCODE_ASSIGN_CONST:
        v_registers[NIBBLE(instruction, 2)] = BYTE(instruction, 0);
        break;

    case SIPEIT_OPCODE_ADD_CONST:
        v_registers[NIBBLE(instruction, 2)] += BYTE(instruction, 0);
        break;

    case SIPEIT_OPCODE_ARITHMETIC:
    {
        uint8_t vx_index = NIBBLE(instruction, 2);
        uint8_t vy_index = NIBBLE(instruction, 1);
        uint8_t op       = NIBBLE(instruction, 0);;

        switch (op)
        {
        case SIPEIT_ARITHMETIC_ASSIGN:
            v_registers[vx_index] = v_registers[vy_index];
            break;

        case SIPEIT_ARITHMETIC_OR:
            v_registers[vx_index] |= v_registers[vy_index];
            break;

        case SIPEIT_ARITHMETIC_AND:
            v_registers[vx_index] &= v_registers[vy_index];
            break;

        case SIPEIT_ARITHMETIC_XOR:
            v_registers[vx_index] ^= v_registers[vy_index];
            break;

        case SIPEIT_ARITHMETIC_ADD:
            v_registers[0xF] = __builtin_add_overflow(
                v_registers[vx_index],
                v_registers[vy_index],
                &v_registers[vx_index]
            );
            break;

        case SIPEIT_ARITHMETIC_SUB:
            v_registers[0xF] = __builtin_sub_overflow(
                v_registers[vx_index],
                v_registers[vy_index],
                &v_registers[vx_index]
            );
            break;

        case SIPEIT_ARITHMETIC_SHIFTR:
            v_registers[0xF] = v_registers[vx_index] & 0b1;
            v_registers[vx_index] >>= 1;
            break;

        case SIPEIT_ARITHMETIC_RSUB:
            v_registers[0xF] = __builtin_sub_overflow(
                v_registers[vx_index],
                v_registers[vy_index],
                &v_registers[vx_index]
            );
            break;

        case SIPEIT_ARITHMETIC_SHIFTL:
            v_registers[0xF] = v_registers[vx_index] & 0b1;
            v_registers[vx_index] <<= 1;
            break;

        default:
            CRASH_INVALID_INSTRUCTION();
            break;
        }
    }
    break;

    case SIPEIT_OPCODE_ASSIGN_I:
        address_register = TRIBBLE(instruction, 0);
        break;

    case SIPEIT_OPCODE_RELATIVE_JUMP:
        instruction_pointer = v_registers[0] + TRIBBLE(instruction, 0);
        break;

    case SIPEIT_OPCODE_RAND:
    {
        uint8_t value = BYTE(instruction, 0);
        v_registers[NIBBLE(instruction, 2)] = (uint8_t)(rand() % 255) & value;
    }
    break;

    case SIPEIT_OPCODE_DRAW:
    {
        uint8_t cx = v_registers[NIBBLE(instruction, 2)];
        uint8_t cy = v_registers[NIBBLE(instruction, 1)];
        const uint8_t rows = NIBBLE(instruction, 0);

        const uint8_t *sprite = &memory[address_register];

        v_registers[0xF] = 0;

        for (uint8_t y_offset = 0; y_offset < rows; y_offset++, sprite++) {
            const uint8_t src_pixels = *sprite;

            for (uint8_t x_offset = 0; x_offset < 8; x_offset++)
            {
                uint32_t *dest_pixel = &vmemory[(cy + y_offset) % SIPEIT_DISPLAY_HEIGHT][(cx + x_offset) % SIPEIT_DISPLAY_WIDTH];
                uint32_t new_pixel  = ((src_pixels & (0x80 >> x_offset)) != 0) * UINT32_MAX;

                v_registers[0xF] = v_registers[0xF] || (*dest_pixel ^ new_pixel) == 0;

                *dest_pixel ^= new_pixel;
            }
        }
    }
    break;

    case SIPEIT_OPCODE_KEY_PRESSED:
    {
        uint8_t key = v_registers[NIBBLE(instruction, 2)];
        uint8_t option = BYTE(instruction, 0);

        switch (option)
        {
        case 0x9E:
            SKIP_INSTR_IF(keys[key] == 0xFF);
            break;

        case 0xA1:
            SKIP_INSTR_IF(keys[key] == 0x00);
            break;

        default:
            CRASH_INVALID_INSTRUCTION();
            break;
        }
    }
    break;

    case SIPEIT_OPCODE_MISC:
    {
        uint8_t value = NIBBLE(instruction, 2);
        uint8_t option = BYTE(instruction, 0);

        switch (option)
        {
        case SIPEIT_MISC_WAIT_KEY:
            waiting_for_key = true;
            v_register_key_index = value;
            break;

        case SIPEIT_MISC_GET_DELAY:
            v_registers[value] = delay_timer;
            break;

        case SIPEIT_MISC_SET_ST:
            sound_timer = v_registers[value];
            break;

        case SIPEIT_MISC_SET_DT:
            delay_timer = v_registers[value];
            break;

        case SIPEIT_MISC_ADD_I:
            address_register += v_registers[value];
            break;

        case SIPEIT_MISC_SET_SPRITE:
            address_register = 0 /* offset */ + 5 * v_registers[value];
            break;

        case SIPEIT_MISC_REG_DUMP:
            memcpy(memory + address_register, v_registers, value + 1);
            break;

        case SIPEIT_MISC_REG_LOAD:
            memcpy(v_registers, memory + address_register, value + 1);
            break;

        case SIPEIT_MISC_BCD:
        {
            const uint8_t reg_value = v_registers[value];

            memory[address_register] = (reg_value / 100);
            memory[address_register + 1] = (reg_value / 10) % 10;
            memory[address_register + 2] = (reg_value % 10);
            break;
        }

        default:
            CRASH_INVALID_INSTRUCTION();
            break;
        }
    }
    break;

    default:
        CRASH_INVALID_INSTRUCTION();
        break;
    }

    INC_INSTR_POINTER();
}


inline static void update_timers()
{
    static clock_t prev_clock = 0;

    if (!prev_clock) {
        prev_clock = clock();
        return;
    }

    clock_t curr_clock = clock();
    double diff = ((double)(curr_clock - prev_clock) / CLOCKS_PER_SEC);

    if (diff > SIPEIT_TIMERS_INTERVAL)
    {
        delay_timer -= (delay_timer != 0) * 1;
        sound_timer -= (sound_timer != 0) * 1;

        prev_clock = clock();
    }
}

inline static void load_program(const char *path)
{
    FILE *file = fopen(path, "r");

    if (file == NULL)
        CRASHFMT(SIPEIT_ERROR_FILE, "%s", strerror(errno));

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    size_t total_read = fread(
        memory + SIPEIT_PROGRAM_OFFSET,
        1,
        SIPEIT_TOTAL_MEMORY - SIPEIT_PROGRAM_OFFSET,
        file);

    if (file_size > total_read)
        CRASH(SIPEIT_ERROR_FILE, "file is too big.");
}

inline static void initialize_memory()
{
    static const uint8_t fontset[80] =
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

    memcpy(memory, fontset, sizeof(fontset));

}

#include <SDL2/SDL.h>

int main(int argc, char **argv)
{
    srand(time(NULL));
    initialize_memory();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
        CRASHFMT(-1, "Could not initialize SDL2 (%s)", SDL_GetError());

    if (argc == 1)
        CRASH(SIPEIT_ERROR_MISSING_ARGUMENT, "missing argument.");

    load_program(argv[1]);

    SDL_Window *window = SDL_CreateWindow("Sipeit", 0, 0, 800, 600, 0);

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_RenderSetLogicalSize(renderer, 800, 600);

    SDL_Texture* texture = SDL_CreateTexture(renderer,
       SDL_PIXELFORMAT_ABGR8888,
       SDL_TEXTUREACCESS_STREAMING,
       SIPEIT_DISPLAY_WIDTH,
       SIPEIT_DISPLAY_HEIGHT
   );

    for(;;) {
        cpu_step();
        update_timers();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                SDL_Quit();
                exit(0);
                break;

            case SDL_KEYDOWN:
            {
                uint8_t key;

                switch (event.key.keysym.sym)
                {
                case SDLK_x:
                    key = 0x0;
                    break;

                case SDLK_1:
                    key = 0x1;
                    break;

                case SDLK_2:
                    key = 0x2;
                    break;

                case SDLK_3:
                    key = 0x3;
                    break;
                
                case SDLK_q:
                    key = 0x4;
                    break;

                case SDLK_w:
                    key = 0x5;
                    break;

                case SDLK_e:
                    key = 0x6;
                    break;

                case SDLK_a:
                    key = 0x7;
                    break;

                case SDLK_s:
                    key = 0x8;
                    break;

                case SDLK_d:
                    key = 0x9;
                    break;

                case SDLK_z:
                    key = 0xA;
                    break;

                case SDLK_c:
                    key = 0xB;
                    break;

                case SDLK_4:
                    key = 0xC;
                    break;

                case SDLK_r:
                    key = 0xD;
                    break;

                case SDLK_f:
                    key = 0xE;
                    break;

                case SDLK_v:
                    key = 0xF;
                    break;

                default:
                    continue;
                }

                keys[key] = 0xFF;

                if (waiting_for_key) {
                    waiting_for_key = false;
                    v_register_key_index = key;
                }
            }
            break;

            case SDL_KEYUP:
            {
                uint8_t key;

                switch (event.key.keysym.sym)
                {
                case SDLK_x:
                    key = 0x0;
                    break;

                case SDLK_1:
                    key = 0x1;
                    break;

                case SDLK_2:
                    key = 0x2;
                    break;

                case SDLK_3:
                    key = 0x3;
                    break;
                
                case SDLK_q:
                    key = 0x4;
                    break;

                case SDLK_w:
                    key = 0x5;
                    break;

                case SDLK_e:
                    key = 0x6;
                    break;

                case SDLK_a:
                    key = 0x7;
                    break;

                case SDLK_s:
                    key = 0x8;
                    break;

                case SDLK_d:
                    key = 0x9;
                    break;

                case SDLK_z:
                    key = 0xA;
                    break;

                case SDLK_c:
                    key = 0xB;
                    break;

                case SDLK_4:
                    key = 0xC;
                    break;

                case SDLK_r:
                    key = 0xD;
                    break;

                case SDLK_f:
                    key = 0xE;
                    break;

                case SDLK_v:
                    key = 0xF;
                    break;

                default:
                    continue;
                }

                keys[key] = 0x00;
            }
            break;

            default:
                break;
            }
        }

        SDL_UpdateTexture(texture, NULL, vmemory, SIPEIT_DISPLAY_WIDTH * sizeof(uint32_t));

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        usleep(2000);
    }

    return 0;
}
