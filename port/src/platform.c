/*
 * platform.c - SDL2 front end: window, presenting the emulated CGA screen,
 * events, and the game thread.
 *
 * The translated game runs on its own thread, like the original program on
 * the CPU, while this (main) thread does what the hardware did concurrently:
 * it scans out cga_vram at the display rate and delivers key events to the
 * game's keyboard table (kbd_scancode). The speaker is rendered on SDL's
 * audio thread (speaker.c).
 *
 * The web build (Emscripten) has no game thread: the browser calls
 * web_frame, which handles events, runs the game for a slice (game_run in
 * the C translation, see bios.h) and presents the screen.
 */
#include "bios.h"
#include "font8x8.h"
#include "speaker.h"
#ifdef ALLEYCAT_EXTERNAL_DATA
#include "game_data.h"
#endif

#include <SDL2/SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define CGA_WIDTH 320
#define CGA_HEIGHT 200
#define CGA_BYTES_PER_ROW 80
#define CGA_ODD_BANK 0x2000
#define WINDOW_SCALE 4

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static uint32_t pixels[CGA_WIDTH * CGA_HEIGHT];
static SDL_atomic_t game_finished;

/* Main loop state, main thread only. */
static bool quit;
static bool force;          /* present even an unchanged frame */
static bool fullscreen;

/* SDL (USB HID) scancode -> XT set 1 scancode; 0 = no XT equivalent. Keys are
 * mapped by position, as the original reads raw scancodes. The keypad gives
 * the same codes as the arrow/navigation block, as on the XT keyboard. */
static const uint8_t xt_scancode[SDL_NUM_SCANCODES] = {
    [SDL_SCANCODE_ESCAPE] = 0x01,
    /* 1-4 are aliases for K H T A, the skill keys of the options menu; the
     * game reads no other keys there and ignores digits other than 9. */
    [SDL_SCANCODE_1] = 0x25, [SDL_SCANCODE_2] = 0x23, [SDL_SCANCODE_3] = 0x14,
    [SDL_SCANCODE_4] = 0x1E, [SDL_SCANCODE_5] = 0x06, [SDL_SCANCODE_6] = 0x07,
    [SDL_SCANCODE_7] = 0x08, [SDL_SCANCODE_8] = 0x09, [SDL_SCANCODE_9] = 0x0A,
    [SDL_SCANCODE_0] = 0x0B, [SDL_SCANCODE_MINUS] = 0x0C, [SDL_SCANCODE_EQUALS] = 0x0D,
    [SDL_SCANCODE_BACKSPACE] = 0x0E, [SDL_SCANCODE_TAB] = 0x0F,
    [SDL_SCANCODE_Q] = 0x10, [SDL_SCANCODE_W] = 0x11, [SDL_SCANCODE_E] = 0x12,
    [SDL_SCANCODE_R] = 0x13, [SDL_SCANCODE_T] = 0x14, [SDL_SCANCODE_Y] = 0x15,
    [SDL_SCANCODE_U] = 0x16, [SDL_SCANCODE_I] = 0x17, [SDL_SCANCODE_O] = 0x18,
    [SDL_SCANCODE_P] = 0x19, [SDL_SCANCODE_LEFTBRACKET] = 0x1A, [SDL_SCANCODE_RIGHTBRACKET] = 0x1B,
    [SDL_SCANCODE_RETURN] = 0x1C, [SDL_SCANCODE_KP_ENTER] = 0x1C,
    [SDL_SCANCODE_LCTRL] = 0x1D, [SDL_SCANCODE_RCTRL] = 0x1D,
    [SDL_SCANCODE_A] = 0x1E, [SDL_SCANCODE_S] = 0x1F, [SDL_SCANCODE_D] = 0x20,
    [SDL_SCANCODE_F] = 0x21, [SDL_SCANCODE_G] = 0x22, [SDL_SCANCODE_H] = 0x23,
    [SDL_SCANCODE_J] = 0x24, [SDL_SCANCODE_K] = 0x25, [SDL_SCANCODE_L] = 0x26,
    [SDL_SCANCODE_SEMICOLON] = 0x27, [SDL_SCANCODE_APOSTROPHE] = 0x28, [SDL_SCANCODE_GRAVE] = 0x29,
    [SDL_SCANCODE_LSHIFT] = 0x2A, [SDL_SCANCODE_BACKSLASH] = 0x2B,
    [SDL_SCANCODE_Z] = 0x2C, [SDL_SCANCODE_X] = 0x2D, [SDL_SCANCODE_C] = 0x2E,
    [SDL_SCANCODE_V] = 0x2F, [SDL_SCANCODE_B] = 0x30, [SDL_SCANCODE_N] = 0x31,
    [SDL_SCANCODE_M] = 0x32, [SDL_SCANCODE_COMMA] = 0x33, [SDL_SCANCODE_PERIOD] = 0x34,
    [SDL_SCANCODE_SLASH] = 0x35, [SDL_SCANCODE_RSHIFT] = 0x36, [SDL_SCANCODE_KP_MULTIPLY] = 0x37,
    [SDL_SCANCODE_LALT] = 0x38, [SDL_SCANCODE_RALT] = 0x38, [SDL_SCANCODE_SPACE] = 0x39,
    [SDL_SCANCODE_CAPSLOCK] = 0x3A,
    [SDL_SCANCODE_F1] = 0x3B, [SDL_SCANCODE_F2] = 0x3C, [SDL_SCANCODE_F3] = 0x3D,
    [SDL_SCANCODE_F4] = 0x3E, [SDL_SCANCODE_F5] = 0x3F, [SDL_SCANCODE_F6] = 0x40,
    [SDL_SCANCODE_F7] = 0x41, [SDL_SCANCODE_F8] = 0x42, [SDL_SCANCODE_F9] = 0x43,
    [SDL_SCANCODE_F10] = 0x44,
    [SDL_SCANCODE_HOME] = 0x47, [SDL_SCANCODE_KP_7] = 0x47,
    [SDL_SCANCODE_UP] = 0x48, [SDL_SCANCODE_KP_8] = 0x48,
    [SDL_SCANCODE_PAGEUP] = 0x49, [SDL_SCANCODE_KP_9] = 0x49,
    [SDL_SCANCODE_KP_MINUS] = 0x4A,
    [SDL_SCANCODE_LEFT] = 0x4B, [SDL_SCANCODE_KP_4] = 0x4B,
    [SDL_SCANCODE_KP_5] = 0x4C,
    [SDL_SCANCODE_RIGHT] = 0x4D, [SDL_SCANCODE_KP_6] = 0x4D,
    [SDL_SCANCODE_KP_PLUS] = 0x4E,
    [SDL_SCANCODE_END] = 0x4F, [SDL_SCANCODE_KP_1] = 0x4F,
    [SDL_SCANCODE_DOWN] = 0x50, [SDL_SCANCODE_KP_2] = 0x50,
    [SDL_SCANCODE_PAGEDOWN] = 0x51, [SDL_SCANCODE_KP_3] = 0x51,
    [SDL_SCANCODE_INSERT] = 0x52, [SDL_SCANCODE_KP_0] = 0x52,
    [SDL_SCANCODE_DELETE] = 0x53, [SDL_SCANCODE_KP_PERIOD] = 0x53,
};

/* The 16 RGBI colors of a CGA monitor, as XRGB8888. */
static const uint32_t cga_rgbi[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

/* Ctrl-P: show the colors the game sets on a PCjr instead of the CGA palette.
 * Display only; the game keeps running its CGA code. Main thread only. */
static bool pcjr_colors;

/* The Ctrl-P choice ("pcjr") and window or fullscreen ("fullscreen") are each
 * kept in a one-byte file ('0' or '1') in the per-user SDL preferences
 * folder, so the next start uses them. Failures are ignored: the game then
 * starts with the defaults (CGA colors, fullscreen). */
static SDL_RWops *open_setting(const char *name, const char *mode)
{
    char *dir = SDL_GetPrefPath("petarov", "alleycat");
    if (dir == NULL)
        return NULL;
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s%s", dir, name);
    SDL_free(dir);
    return SDL_RWFromFile(path, mode);
}

static bool load_setting(const char *name, bool default_value)
{
    SDL_RWops *f = open_setting(name, "rb");
    char c = default_value ? '1' : '0';
    if (f != NULL) {
        SDL_RWread(f, &c, 1, 1);
        SDL_RWclose(f);
    }
    return c == '1';
}

static void save_setting(const char *name, bool value)
{
    SDL_RWops *f = open_setting(name, "wb");
    if (f != NULL) {
        char c = value ? '1' : '0';
        SDL_RWwrite(f, &c, 1, 1);
        SDL_RWclose(f);
    }
}

static void build_palette(uint32_t out[4])
{
    uint8_t sel = bios_cga_color_select();
    int bright = (sel & 0x10) ? 8 : 0;
    out[0] = cga_rgbi[sel & 0x0F];
    if (pcjr_colors) {
        for (int reg = 1; reg <= 3; reg++)
            out[reg] = cga_rgbi[bios_pcjr_color(reg)];
    } else if (sel & 0x20) {
        out[1] = cga_rgbi[3 + bright];  /* cyan */
        out[2] = cga_rgbi[5 + bright];  /* magenta */
        out[3] = cga_rgbi[7 + bright];  /* white */
    } else {
        out[1] = cga_rgbi[2 + bright];  /* green */
        out[2] = cga_rgbi[4 + bright];  /* red */
        out[3] = cga_rgbi[6 + bright];  /* brown/yellow */
    }
}

static bool platform_init(bool fullscreen)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    window = SDL_CreateWindow("Alley Cat", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              CGA_WIDTH * WINDOW_SCALE, CGA_HEIGHT * WINDOW_SCALE,
                              SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (window == NULL) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    if (fullscreen)
        SDL_ShowCursor(SDL_DISABLE);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }
    if (SDL_RenderSetLogicalSize(renderer, CGA_WIDTH, CGA_HEIGHT) != 0 ||
        SDL_RenderSetIntegerScale(renderer, SDL_TRUE) != 0) {
        SDL_Log("SDL_RenderSetLogicalSize failed: %s", SDL_GetError());
        return false;
    }
    /* SDL_PIXELFORMAT_RGB888 is SDL2's name for XRGB8888. */
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING,
                                CGA_WIDTH, CGA_HEIGHT);
    if (texture == NULL) {
        SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    if (!speaker_init())
        SDL_Log("continuing without sound");
    return true;
}

static void platform_shutdown(void)
{
    speaker_shutdown();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

/* Draws text into pixels with the BIOS font; overlay only, cga_vram is untouched. */
static void draw_text(int x, int y, const char *s, uint32_t color)
{
    for (; *s != '\0'; s++, x += 8) {
        const uint8_t *g = font8x8_glyph((uint8_t)*s);
        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                if (g[r] & (0x80 >> c))
                    pixels[(y + r) * CGA_WIDTH + x + c] = color;
            }
        }
    }
}

/* Converts cga_vram (320x200, 2 bpp, interlaced) to RGB and shows it. A
 * frame identical to the last one shown is skipped unless force is set (window
 * events), so a static screen costs almost nothing, e.g. over remote desktop. */
static void present(bool force)
{
    static uint32_t shown[CGA_WIDTH * CGA_HEIGHT];

    uint32_t pal[4];
    build_palette(pal);

    for (int y = 0; y < CGA_HEIGHT; y++) {
        const uint8_t *row = cga_vram + (y & 1) * CGA_ODD_BANK + (y >> 1) * CGA_BYTES_PER_ROW;
        uint32_t *out = pixels + y * CGA_WIDTH;
        for (int x = 0; x < CGA_BYTES_PER_ROW; x++) {
            uint8_t b = row[x];
            out[0] = pal[(b >> 6) & 3];
            out[1] = pal[(b >> 4) & 3];
            out[2] = pal[(b >> 2) & 3];
            out[3] = pal[b & 3];
            out += 4;
        }
    }
    if (game_in_title) {
        static const char version[] = "v" ALLEYCAT_VERSION;
        draw_text(CGA_WIDTH - 8 * ((int)sizeof version - 1), CGA_HEIGHT - 8, version, pal[3]);
    }

    if (!force && SDL_memcmp(shown, pixels, sizeof shown) == 0)
        return;
    SDL_memcpy(shown, pixels, sizeof shown);

    SDL_UpdateTexture(texture, NULL, pixels, CGA_WIDTH * sizeof(uint32_t));
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

/* F10, Ctrl-W and Ctrl-Q quit everywhere; Esc only on the title screen,
 * elsewhere it reaches the game, which pauses ("paws mode"). */
static bool is_quit_key(const SDL_KeyboardEvent *k)
{
    if (k->keysym.scancode == SDL_SCANCODE_F10)
        return true;
    if ((k->keysym.mod & KMOD_CTRL) && (k->keysym.sym == SDLK_w || k->keysym.sym == SDLK_q))
        return true;
    return k->keysym.scancode == SDL_SCANCODE_ESCAPE && game_in_title;
}

/* Alt-Enter or F11 (Cmd-Enter or Cmd-F11 on macOS, where F11 alone belongs
 * to the system) switch between window and fullscreen. */
static bool is_fullscreen_key(const SDL_KeyboardEvent *k)
{
    SDL_Scancode sc = k->keysym.scancode;
    if (sc != SDL_SCANCODE_RETURN && sc != SDL_SCANCODE_KP_ENTER && sc != SDL_SCANCODE_F11)
        return false;
#ifdef __APPLE__
    return (k->keysym.mod & KMOD_GUI) != 0;
#else
    return sc == SDL_SCANCODE_F11 || (k->keysym.mod & KMOD_ALT) != 0;
#endif
}

#ifndef __EMSCRIPTEN__
/* Fullscreen at the desktop resolution; the logical size keeps the picture
 * scaled and centered. The mouse cursor is hidden there, the game has no use
 * for it. */
static bool set_fullscreen(bool on)
{
    if (SDL_SetWindowFullscreen(window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        SDL_Log("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
        return false;
    }
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
    return true;
}
#endif

#ifdef __EMSCRIPTEN__
/* The page goes fullscreen, not the canvas, and its CSS keeps the canvas
 * scaled to fit. Called shortly after the key press, while the browser
 * still treats the page as user-activated. */
EM_JS(void, web_toggle_fullscreen, (void), {
    if (document.fullscreenElement)
        document.exitFullscreen();
    else
        document.documentElement.requestFullscreen().catch(function () {});
});
#endif

/* One SDL event: quit, fullscreen and the port's hotkeys, keys for the game. */
static void handle_event(const SDL_Event *e)
{
    if (e->type == SDL_QUIT) {
        quit = true;
    } else if (e->type == SDL_WINDOWEVENT || e->type == SDL_RENDER_TARGETS_RESET) {
        force = true;
    } else if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
        SDL_Scancode sc = e->key.keysym.scancode;
        if (e->type == SDL_KEYDOWN && is_quit_key(&e->key)) {
#ifndef __EMSCRIPTEN__
            quit = true;    /* a web page cannot quit: the key is dropped */
#endif
        } else if (is_fullscreen_key(&e->key)) {
            /* Not passed to the game. */
            if (e->type == SDL_KEYDOWN && !e->key.repeat) {
#ifdef __EMSCRIPTEN__
                web_toggle_fullscreen();
#else
                if (set_fullscreen(!fullscreen)) {
                    fullscreen = !fullscreen;
                    save_setting("fullscreen", fullscreen);
                }
#endif
            }
        } else if (xt_scancode[sc] != 0) {
            if (sc == SDL_SCANCODE_L)
                kbd_set_add_life(e->type == SDL_KEYDOWN && (e->key.keysym.mod & KMOD_CTRL));
            if (sc == SDL_SCANCODE_K)
                kbd_set_infinite_lives(e->type == SDL_KEYDOWN && (e->key.keysym.mod & KMOD_CTRL));
            if (sc == SDL_SCANCODE_I)
                kbd_set_invincible(e->type == SDL_KEYDOWN && (e->key.keysym.mod & KMOD_CTRL));
            if (sc == SDL_SCANCODE_P && e->type == SDL_KEYDOWN && !e->key.repeat &&
                (e->key.keysym.mod & KMOD_CTRL)) {
                pcjr_colors = !pcjr_colors;
                save_setting("pcjr", pcjr_colors);
                force = true;
            }
            /* Auto-repeat is passed on, like XT typematic make codes. */
            kbd_scancode(xt_scancode[sc] | (e->type == SDL_KEYUP ? 0x80 : 0));
        }
    }
}

#ifdef __EMSCRIPTEN__
/* One pass of the browser's main loop: events, a slice of the game, and the
 * screen at most 60 times a second. When the game waits (bios.c), the next
 * pass comes after that wait, otherwise as soon as the browser is free. */
static void web_frame(void)
{
    static Uint64 last_present;
    SDL_Event e;
    while (SDL_PollEvent(&e))
        handle_event(&e);
    bios_slice_begin();
    game_yield = 0;
    if (!game_run())
        game_start();   /* the game exited to DOS: start over */
    uint32_t ms = bios_slice_sleep_ms();
    if (ms > 0)
        emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, (int)ms);
    else
        emscripten_set_main_loop_timing(EM_TIMING_SETIMMEDIATE, 0);
    Uint64 now = SDL_GetPerformanceCounter();
    if (force || now - last_present >= SDL_GetPerformanceFrequency() / 60) {
        present(force);
        force = false;
        last_present = now;
    }
}
#endif

static int game_thread(void *unused)
{
    (void)unused;
    game_main();
    SDL_AtomicSet(&game_finished, 1);
    return 0;
}

int main(int argc, char *argv[])
{
#ifdef ALLEYCAT_EXTERNAL_DATA
    /* Initialise Cocoa through SDL before showing a native panel, but wait
     * to create the fullscreen game window until after import. */
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return 1;
    if (!game_data_prepare()) {
        SDL_Quit();
        return 0;
    }
#endif
    /* -pcjr starts with the PCjr colors on (Ctrl-P) and --window in a window,
     * without saving either choice. Other arguments are ignored. */
    bool force_pcjr = false, force_window = false;
    for (int i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "-pcjr") == 0)
            force_pcjr = true;
        else if (SDL_strcmp(argv[i], "--window") == 0)
            force_window = true;
    }
#ifdef __EMSCRIPTEN__
    /* A page can only go fullscreen on a key press or click, and has no
     * preferences folder that outlives it: start in the page, with the
     * PCjr colors (Ctrl-P switches to CGA). */
    (void)force_window;
    fullscreen = false;
#else
    /* SDL_GetPrefPath works before SDL_Init. */
    fullscreen = !force_window && load_setting("fullscreen", true);
#endif
    if (!platform_init(fullscreen))
        return 1;
#ifdef __EMSCRIPTEN__
    pcjr_colors = force_pcjr || load_setting("pcjr", true);
#else
    pcjr_colors = force_pcjr || load_setting("pcjr", false);
#endif
    bios_init();

#ifdef __EMSCRIPTEN__
    /* With text input off, SDL keeps the browser from acting on the keys
     * (Ctrl-R reload, Ctrl-P print, ...) where the browser allows it. */
    SDL_StopTextInput();
    game_start();
    emscripten_set_main_loop(web_frame, 0, false);
    return 0;
#endif

    SDL_Thread *thread = SDL_CreateThread(game_thread, "game", NULL);
    if (thread == NULL) {
        SDL_Log("SDL_CreateThread failed: %s", SDL_GetError());
        platform_shutdown();
        return 1;
    }
    SDL_DetachThread(thread);

    force = true;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 frame = freq / 60;
    Uint64 next_frame = SDL_GetPerformanceCounter();
    while (!quit && !SDL_AtomicGet(&game_finished)) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            handle_event(&e);
        present(force);
        force = false;
        /* At most 60 frames per second: vsync does not always wait (e.g. over
         * xrdp or VNC), and the loop would then spin and redraw nonstop. */
        next_frame += frame;
        Uint64 now = SDL_GetPerformanceCounter();
        if (now < next_frame)
            SDL_Delay((Uint32)((next_frame - now) * 1000 / freq));
        else if (now - next_frame > frame)
            next_frame = now;   /* fell behind (e.g. window dragged): resync */
    }
    /* The game thread may still be running (it busy-waits like the original);
     * returning from main ends the process. */
    platform_shutdown();
    return 0;
}
