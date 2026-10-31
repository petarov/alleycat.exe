/* Headless integration test: title, skill selection, start and cursor input. */
#include "bios.h"
#include <SDL2/SDL.h>
#include <stdio.h>

extern uint8_t ds_base[];
static SDL_atomic_t finished;

static int run_game(void *unused)
{
    (void)unused;
    game_main();
    SDL_AtomicSet(&finished, 1);
    return 0;
}

static void press(uint8_t code)
{
    kbd_scancode(code);
    SDL_Delay(250);
    kbd_scancode(code | 0x80);
}

static unsigned cat_x(void)
{
    return ds_base[0x579] | ds_base[0x57A] << 8;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <vram.bin>\n", argv[0]);
        return 2;
    }
    bios_init();
    SDL_Thread *thread = SDL_CreateThread(run_game, "game", NULL);
    if (thread == NULL) {
        fprintf(stderr, "SDL_CreateThread: %s\n", SDL_GetError());
        return 1;
    }
    SDL_DetachThread(thread);
    SDL_Delay(2000);
    if (!game_in_title || SDL_AtomicGet(&finished)) {
        fprintf(stderr, "gameplay: title was not reached\n");
        return 1;
    }
    press(0x39);                     /* Space: leave title. */
    SDL_Delay(1000);
    press(0x25);                     /* K: Kitten difficulty. */
    SDL_Delay(1000);
    press(0x39);                     /* Space: begin game. */
    SDL_Delay(2000);
    unsigned before = cat_x();
    kbd_scancode(0x4D);              /* Right cursor, held through several ticks. */
    SDL_Delay(700);
    kbd_scancode(0xCD);
    unsigned after = cat_x();
    if (SDL_AtomicGet(&finished) || game_in_title || ds_base[0x1F80] == 0 || before == after) {
        fprintf(stderr, "gameplay: failed (title=%u, lives=%u, x=%u -> %u)\n",
                game_in_title, ds_base[0x1F80], before, after);
        return 1;
    }
    FILE *out = fopen(argv[1], "wb");
    if (out == NULL)
        return 1;
    int ok = fwrite(cga_vram, 1, 0x4000, out) == 0x4000;
    if (fclose(out) != 0)
        ok = 0;
    printf("gameplay: title, Kitten, start, movement %u -> %u; lives=%u\n",
           before, after, ds_base[0x1F80]);
    return ok ? 0 : 1;
}
