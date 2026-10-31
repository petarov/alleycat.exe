/* The same title check as check_title.asm, using the portable C runtime. */
#include "runtime.h"

static void call_at(uint32_t address)
{
    sp = 0;
    push(UINT32_MAX);
    pc = address;
    while (game_run())
        game_yield = 0;
}

void check_title(void)
{
    static const uint16_t blits[][3] = {
        {0x6152, 0x1D0B, 0x00BD}, {0x63D0, 0x160E, 0x069E},
        {0x6638, 0x0C03, 0x0A78}, {0x6680, 0x080E, 0x0CA8},
        {0x6760, 0x0B0C, 0x1D6E}, {0x6868, 0x0804, 0x1DEC},
    };
    game_start();
    wr16(ds_base, 0x2AE5, 0xFA59);
    wr16(ds_base, 0x0008, 0);
    wr16(ds_base, 0x6DF8, 0);
    call_at(0x2A30);
    for (unsigned i = 0; i < sizeof blits / sizeof blits[0]; i++) {
        SI = blits[i][0];
        CX = blits[i][1];
        DI = blits[i][2];
        call_at(0x2D9D);
    }
}
