/*
 * xp-launcher — big-picture entertainment launcher for the XP box.
 *
 * Target: Windows XP, Acer AO751h (Atom Z520, GMA 500). Cross-compiled from
 * Linux with i686-w64-mingw32-gcc against SDL2. Software renderer on a
 * fullscreen-desktop window: driver-safe on Poulsbo and visible to GDI
 * screen capture (nircmd/xpshot), unlike exclusive D3D fullscreen.
 *
 * Rendering model: every screen state is a full-screen BMP pre-rendered on
 * the Linux laptop (scripts/gen-assets.py) — the box just blits one texture
 * per frame. All layout/beauty decisions live in the Python pipeline.
 *
 * v1: home tiles + games list from assets/games.cfg; Enter launches the
 * entry (CreateProcess + wait), the launcher minimizes while the child
 * runs and takes the screen back when it exits.
 */
#include <SDL.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define MAX_GAMES 64

typedef struct {
    char exe[512];
    char args[512];
    char cwd[512];
} Entry;

static Entry music, movies;
static Entry games[MAX_GAMES];
static int n_games = 0;

static SDL_Texture *tex_home[3];
static SDL_Texture *tex_list[MAX_GAMES];
static SDL_Texture *tex_launch;

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

static SDL_Texture *load_bmp(SDL_Renderer *ren, const char *dir,
                             const char *name)
{
    char path[600];
    SDL_snprintf(path, sizeof path, "%s\\assets\\%s", dir, name);
    SDL_Surface *s = SDL_LoadBMP(path);
    if (!s) {
        SDL_Log("load_bmp %s: %s", path, SDL_GetError());
        return NULL;
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    return t;
}

/* games.cfg: one entry per line, "key|exe|args|cwd". CRLF-tolerant. */
static void load_cfg(const char *dir)
{
    char path[600];
    SDL_snprintf(path, sizeof path, "%s\\assets\\games.cfg", dir);
    FILE *f = fopen(path, "r");
    if (!f) {
        SDL_Log("load_cfg: cannot open %s", path);
        return;
    }
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        char *key = line;
        char *exe = strchr(key, '|');
        if (!exe) continue;
        *exe++ = '\0';
        char *args = strchr(exe, '|');
        if (!args) continue;
        *args++ = '\0';
        char *cwd = strchr(args, '|');
        if (!cwd) continue;
        *cwd++ = '\0';

        Entry *e = NULL;
        if (strcmp(key, "music") == 0) e = &music;
        else if (strcmp(key, "movies") == 0) e = &movies;
        else if (strcmp(key, "game") == 0 && n_games < MAX_GAMES)
            e = &games[n_games++];
        if (!e) continue;
        SDL_strlcpy(e->exe, exe, sizeof e->exe);
        SDL_strlcpy(e->args, args, sizeof e->args);
        SDL_strlcpy(e->cwd, cwd, sizeof e->cwd);
    }
    fclose(f);
}

/* Remote control: a one-line command dropped into ctl.txt next to the exe
 * (left/right/up/down/enter/back/quit). Written over the SMB share from the
 * laptop; consumed (deleted) once read. Exists because nircmd's synthetic
 * keypresses reach SDL with a null scancode for Return/Escape/Space, so
 * remote testing can't use the keyboard path. Polled a few times a second. */
static void poll_ctl(const char *dir, int *nav_x, int *nav_y,
                     int *accept, int *back, int *quit)
{
    char path[600];
    SDL_snprintf(path, sizeof path, "%s\\ctl.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char cmd[64] = "";
    if (fgets(cmd, sizeof cmd, f))
        cmd[strcspn(cmd, "\r\n")] = '\0';
    fclose(f);
    remove(path);
    if      (strcmp(cmd, "left") == 0)  *nav_x = -1;
    else if (strcmp(cmd, "right") == 0) *nav_x = 1;
    else if (strcmp(cmd, "up") == 0)    *nav_y = -1;
    else if (strcmp(cmd, "down") == 0)  *nav_y = 1;
    else if (strcmp(cmd, "enter") == 0) *accept = 1;
    else if (strcmp(cmd, "back") == 0)  *back = 1;
    else if (strcmp(cmd, "quit") == 0)  *quit = 1;
}

/* Run the entry and block until it exits; launcher minimizes meanwhile. */
static void launch(SDL_Window *win, SDL_Renderer *ren, const Entry *e)
{
    if (!e->exe[0]) return;

    if (tex_launch) {               /* "STARTING…" while the child boots */
        SDL_RenderCopy(ren, tex_launch, NULL, NULL);
        SDL_RenderPresent(ren);
    }

    char cmd[1200];
    if (e->args[0])
        SDL_snprintf(cmd, sizeof cmd, "\"%s\" %s", e->exe, e->args);
    else
        SDL_snprintf(cmd, sizeof cmd, "\"%s\"", e->exe);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);

    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL,
                        e->cwd[0] ? e->cwd : NULL, &si, &pi)) {
        SDL_Log("CreateProcess failed (%lu): %s", GetLastError(), cmd);
        return;
    }
    SDL_MinimizeWindow(win);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    SDL_RestoreWindow(win);
    SDL_RaiseWindow(win);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);  /* drop input queued while away */
}

#define WIN_TITLE "Performa Entertainment System"

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* Single instance: re-running the shortcut focuses the existing one. */
    CreateMutexA(NULL, FALSE, "xp_launcher_single");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowA(NULL, WIN_TITLE);
        if (w) {
            ShowWindow(w, SW_RESTORE);
            SetForegroundWindow(w);
        }
        return 0;
    }

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_Joystick *joy = SDL_NumJoysticks() > 0 ? SDL_JoystickOpen(0) : NULL;

    SDL_Window *win = SDL_CreateWindow(WIN_TITLE,
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
        SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : NULL;
    if (!ren) {
        SDL_Log("window/renderer: %s", SDL_GetError());
        return 1;
    }

    char dir[512];
    exe_dir(dir, sizeof dir);
    load_cfg(dir);

    char name[64];
    for (int i = 0; i < 3; i++) {
        SDL_snprintf(name, sizeof name, "home_%d.bmp", i);
        tex_home[i] = load_bmp(ren, dir, name);
    }
    for (int i = 0; i < n_games; i++) {
        SDL_snprintf(name, sizeof name, "list_%d.bmp", i);
        tex_list[i] = load_bmp(ren, dir, name);
    }
    tex_launch = load_bmp(ren, dir, "launch.bmp");

    enum { HOME, LIST } screen = HOME;
    int sel_home = 0, sel_list = 0;
    int running = 1, axis_latch = 0;

    int ctl_tick = 0;
    while (running) {
        int nav_x = 0, nav_y = 0, accept = 0, back = 0, quit = 0;

        if (++ctl_tick >= 6) {          /* ~5x/sec is plenty */
            ctl_tick = 0;
            poll_ctl(dir, &nav_x, &nav_y, &accept, &back, &quit);
            if (quit) running = 0;
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_KEYDOWN:
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE:            back = 1; break;
                case SDLK_LEFT:              nav_x = -1; break;
                case SDLK_RIGHT:             nav_x = 1; break;
                case SDLK_UP:                nav_y = -1; break;
                case SDLK_DOWN:              nav_y = 1; break;
                case SDLK_RETURN:
                case SDLK_SPACE:             accept = 1; break;
                default: break;
                }
                break;
            case SDL_JOYHATMOTION:
                if (e.jhat.value & SDL_HAT_LEFT)  nav_x = -1;
                if (e.jhat.value & SDL_HAT_RIGHT) nav_x = 1;
                if (e.jhat.value & SDL_HAT_UP)    nav_y = -1;
                if (e.jhat.value & SDL_HAT_DOWN)  nav_y = 1;
                break;
            case SDL_JOYAXISMOTION:
                if (e.jaxis.axis > 1) break;
                if (e.jaxis.value < -16000 && !axis_latch) {
                    if (e.jaxis.axis == 0) nav_x = -1; else nav_y = -1;
                    axis_latch = 1;
                } else if (e.jaxis.value > 16000 && !axis_latch) {
                    if (e.jaxis.axis == 0) nav_x = 1; else nav_y = 1;
                    axis_latch = 1;
                } else if (e.jaxis.value > -8000 && e.jaxis.value < 8000) {
                    axis_latch = 0;
                }
                break;
            case SDL_JOYBUTTONDOWN:
                if (e.jbutton.button == 0) accept = 1;   /* A */
                if (e.jbutton.button == 1) back = 1;     /* B */
                break;
            }
        }

        if (screen == HOME) {
            if (nav_x < 0 && sel_home > 0) sel_home--;
            if (nav_x > 0 && sel_home < 2) sel_home++;
            if (back) running = 0;
            if (accept) {
                if (sel_home == 0 && n_games > 0) { screen = LIST; sel_list = 0; }
                else if (sel_home == 1) launch(win, ren, &music);
                else if (sel_home == 2) launch(win, ren, &movies);
            }
        } else {
            if (nav_y < 0 && sel_list > 0) sel_list--;
            if (nav_y > 0 && sel_list < n_games - 1) sel_list++;
            if (back) screen = HOME;
            if (accept) launch(win, ren, &games[sel_list]);
        }

        SDL_Texture *t = (screen == HOME) ? tex_home[sel_home]
                                          : tex_list[sel_list];
        if (t) SDL_RenderCopy(ren, t, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(33);   /* ~30fps is plenty for a menu; be kind to the Atom */
    }

    if (joy) SDL_JoystickClose(joy);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
