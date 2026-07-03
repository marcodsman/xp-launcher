/*
 * xp-launcher — big-picture entertainment launcher for the XP box.
 *
 * Target: Windows XP, Acer AO751h (Atom Z520, GMA 500). Cross-compiled from
 * Linux with i686-w64-mingw32-gcc against SDL2. Uses the SOFTWARE renderer
 * on a fullscreen-desktop window: driver-safe on Poulsbo and visible to GDI
 * screen capture (nircmd/xpshot), unlike exclusive D3D fullscreen.
 *
 * v0 scope: three section tiles (Games / Music / Movies), keyboard + joystick
 * navigation, selection highlight, Esc quits. Launching comes in v1.
 */
#include <SDL.h>
#include <stdio.h>

#define BG_R 12
#define BG_G 14
#define BG_B 24

typedef struct {
    const char *label_bmp;
    SDL_Texture *label;
    int label_w, label_h;
    Uint8 r, g, b;
} Tile;

static Tile tiles[] = {
    { "games.bmp",  NULL, 0, 0, 0x2e, 0x7d, 0x4f },
    { "music.bmp",  NULL, 0, 0, 0x8a, 0x4f, 0x9e },
    { "movies.bmp", NULL, 0, 0, 0x2f, 0x5e, 0x9e },
};
#define NTILES (int)(sizeof(tiles) / sizeof(tiles[0]))

/* Load a BMP as a texture, treating pure magenta as transparent. */
static SDL_Texture *load_bmp(SDL_Renderer *ren, const char *dir,
                             const char *name, int *w, int *h)
{
    char path[512];
    SDL_snprintf(path, sizeof path, "%s\\assets\\%s", dir, name);
    SDL_Surface *s = SDL_LoadBMP(path);
    if (!s) {
        SDL_Log("load_bmp %s: %s", path, SDL_GetError());
        return NULL;
    }
    SDL_SetColorKey(s, SDL_TRUE, SDL_MapRGB(s->format, 255, 0, 255));
    if (w) *w = s->w;
    if (h) *h = s->h;
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    return t;
}

/* Directory containing the exe, so assets resolve no matter the cwd. */
static void exe_dir(char *out, size_t n)
{
    char *base = SDL_GetBasePath();
    if (base) {
        SDL_strlcpy(out, base, n);
        size_t len = SDL_strlen(out);
        if (len > 0 && (out[len - 1] == '\\' || out[len - 1] == '/'))
            out[len - 1] = '\0';
        SDL_free(base);
    } else {
        SDL_strlcpy(out, ".", n);
    }
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_Joystick *joy = SDL_NumJoysticks() > 0 ? SDL_JoystickOpen(0) : NULL;

    SDL_Window *win = SDL_CreateWindow("Entertainment System",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
        SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) {
        SDL_Log("CreateWindow: %s", SDL_GetError());
        return 1;
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        SDL_Log("CreateRenderer: %s", SDL_GetError());
        return 1;
    }

    int sw, sh;
    SDL_GetRendererOutputSize(ren, &sw, &sh);

    char dir[512];
    exe_dir(dir, sizeof dir);
    for (int i = 0; i < NTILES; i++)
        tiles[i].label = load_bmp(ren, dir, tiles[i].label_bmp,
                                  &tiles[i].label_w, &tiles[i].label_h);
    int title_w = 0, title_h = 0;
    SDL_Texture *title = load_bmp(ren, dir, "title.bmp", &title_w, &title_h);

    /* Tile geometry: three tiles centered, sized off the screen. */
    int tw = sw / 4;
    int th = sh * 2 / 5;
    int gap = tw / 6;
    int total = NTILES * tw + (NTILES - 1) * gap;
    int x0 = (sw - total) / 2;
    int ty = (sh - th) / 2 + sh / 24;

    int sel = 0, running = 1, axis_latch = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                if (e.key.keysym.sym == SDLK_LEFT && sel > 0) sel--;
                if (e.key.keysym.sym == SDLK_RIGHT && sel < NTILES - 1) sel++;
                break;
            case SDL_JOYHATMOTION:
                if (e.jhat.value & SDL_HAT_LEFT && sel > 0) sel--;
                if (e.jhat.value & SDL_HAT_RIGHT && sel < NTILES - 1) sel++;
                break;
            case SDL_JOYAXISMOTION:
                /* latch so one stick push moves one tile */
                if (e.jaxis.axis == 0) {
                    if (e.jaxis.value < -16000 && !axis_latch) {
                        if (sel > 0) sel--;
                        axis_latch = 1;
                    } else if (e.jaxis.value > 16000 && !axis_latch) {
                        if (sel < NTILES - 1) sel++;
                        axis_latch = 1;
                    } else if (e.jaxis.value > -8000 && e.jaxis.value < 8000) {
                        axis_latch = 0;
                    }
                }
                break;
            }
        }

        SDL_SetRenderDrawColor(ren, BG_R, BG_G, BG_B, 255);
        SDL_RenderClear(ren);

        if (title) {
            SDL_Rect tr = { (sw - title_w) / 2, sh / 12, title_w, title_h };
            SDL_RenderCopy(ren, title, NULL, &tr);
        }

        for (int i = 0; i < NTILES; i++) {
            SDL_Rect r = { x0 + i * (tw + gap), ty, tw, th };
            int hot = (i == sel);
            if (hot) {
                /* glow border: a slightly larger bright rect behind */
                SDL_Rect g2 = { r.x - 6, r.y - 6, r.w + 12, r.h + 12 };
                SDL_SetRenderDrawColor(ren, 240, 240, 250, 255);
                SDL_RenderFillRect(ren, &g2);
                r.y -= sh / 60;   /* selected tile lifts slightly */
            }
            Uint8 dim = hot ? 255 : 140;
            SDL_SetRenderDrawColor(ren,
                tiles[i].r * dim / 255, tiles[i].g * dim / 255,
                tiles[i].b * dim / 255, 255);
            SDL_RenderFillRect(ren, &r);
            if (tiles[i].label) {
                SDL_Rect lr = {
                    r.x + (tw - tiles[i].label_w) / 2,
                    r.y + th - tiles[i].label_h - th / 10,
                    tiles[i].label_w, tiles[i].label_h
                };
                SDL_RenderCopy(ren, tiles[i].label, NULL, &lr);
            }
        }

        SDL_RenderPresent(ren);
        SDL_Delay(33);   /* ~30fps is plenty for a menu; be kind to the Atom */
    }

    if (joy) SDL_JoystickClose(joy);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
