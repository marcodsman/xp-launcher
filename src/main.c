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
#include <SDL_syswm.h>
#include <SDL_mixer.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define MAX_GAMES 64
#define MAX_MEDIA 128
#define WIN_TITLE "Performa Entertainment System"

typedef struct {
    char exe[512];
    char args[512];
    char cwd[512];
} Entry;

static Entry music, movies;             /* home-tile default players */
static Entry games[MAX_GAMES];
static int n_games = 0;
/* Media: each entry launches the section player with a file (see load_cfg).
 * `movies`/`music` above are the players; these are the browsable items. */
static Entry movie_items[MAX_MEDIA];
static int n_movies = 0;
static Entry song_items[MAX_MEDIA];
static int n_songs = 0;

static SDL_Texture *tex_home[3];
static SDL_Texture *tex_list[MAX_GAMES];
static SDL_Texture *tex_movie[MAX_MEDIA];
static SDL_Texture *tex_song[MAX_MEDIA];
static SDL_Texture *tex_launch;

/* All connected pads. The dual PS2-style adapter is TWO joystick devices on
 * one USB port — open everything, not just index 0, so both ports work. */
#define MAX_PADS 8
static SDL_GameController *pads[MAX_PADS];
static int n_pads = 0;
static SDL_Joystick *raw_joys[MAX_PADS];
static int n_raw = 0;

static FILE *logf;   /* SDL_Log goes nowhere under -mwindows; tee to a file */

static void log_to_file(void *ud, int cat, SDL_LogPriority pri, const char *msg)
{
    (void)ud; (void)cat; (void)pri;
    if (logf) {
        fprintf(logf, "%s\n", msg);
        fflush(logf);
    }
}

/* Open device index i as a mapped controller if possible, else raw joystick. */
static void open_pad(int i)
{
    if (SDL_IsGameController(i)) {
        SDL_GameController *c = SDL_GameControllerOpen(i);
        if (c && n_pads < MAX_PADS) {
            pads[n_pads++] = c;
            SDL_Log("pad %d: %s (mapped controller)", i,
                    SDL_GameControllerName(c));
        }
    } else {
        SDL_Joystick *j = SDL_JoystickOpen(i);
        if (j && n_raw < MAX_PADS) {
            raw_joys[n_raw++] = j;
            SDL_Log("pad %d: %s (raw joystick)", i, SDL_JoystickName(j));
        }
    }
}

/* Instance id already open? (JOYDEVICEADDED can fire for known devices.) */
static int pad_known(SDL_JoystickID id)
{
    for (int i = 0; i < n_pads; i++)
        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id)
            return 1;
    for (int i = 0; i < n_raw; i++)
        if (SDL_JoystickInstanceID(raw_joys[i]) == id)
            return 1;
    return 0;
}

static void close_pad(SDL_JoystickID id)
{
    for (int i = 0; i < n_pads; i++)
        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id) {
            SDL_GameControllerClose(pads[i]);
            pads[i] = pads[--n_pads];
            SDL_Log("controller instance %d removed", (int)id);
            return;
        }
    for (int i = 0; i < n_raw; i++)
        if (SDL_JoystickInstanceID(raw_joys[i]) == id) {
            SDL_JoystickClose(raw_joys[i]);
            raw_joys[i] = raw_joys[--n_raw];
            SDL_Log("joystick instance %d removed", (int)id);
            return;
        }
}

/* Raw joystick events from a device opened as a GameController would double
 * up with the controller events — skip those. */
static int is_mapped(SDL_JoystickID id)
{
    return SDL_GameControllerFromInstanceID(id) != NULL;
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
        else if (strcmp(key, "movie") == 0 && n_movies < MAX_MEDIA)
            e = &movie_items[n_movies++];
        else if (strcmp(key, "song") == 0 && n_songs < MAX_MEDIA)
            e = &song_items[n_songs++];
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
                     int *accept, int *back, int *quit, int *show)
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
    else if (strcmp(cmd, "show") == 0)  *show = 1;
}

/* UI sound. Subtle blips + a quiet ambient bed (see scripts/gen-sounds.py).
 * audio_ok stays 0 if the box has no working audio device — everything
 * degrades to silent, never fatal. */
static int audio_ok;
static Mix_Chunk *sfx_move, *sfx_select, *sfx_back, *sfx_launch;
static Mix_Music *bgm;

static void sfx(Mix_Chunk *c)
{
    if (audio_ok && c) Mix_PlayChannel(-1, c, 0);
}

static Mix_Chunk *load_sfx(const char *dir, const char *name)
{
    char path[600];
    SDL_snprintf(path, sizeof path, "%s\\assets\\snd\\%s", dir, name);
    Mix_Chunk *c = Mix_LoadWAV(path);
    if (!c) SDL_Log("load_sfx %s: %s", path, Mix_GetError());
    return c;
}

/* The running child (game/player), if any. The launcher does NOT block on
 * it: the main loop keeps pumping (minimized) so SELECT can summon us back,
 * and reap_child() notices the exit. One child at a time. */
static HANDLE child;

/* Start the entry and minimize; the main loop babysits it from here. */
static void launch(SDL_Window *win, SDL_Renderer *ren, const Entry *e)
{
    if (!e->exe[0] || child) return;

    sfx(sfx_launch);
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
    CloseHandle(pi.hThread);
    child = pi.hProcess;
    SDL_Log("launched: %s", cmd);
    if (audio_ok) Mix_PauseMusic();   /* hush the bed while a game runs */
    SDL_MinimizeWindow(win);
}

/* Bring the launcher to the front (SELECT button, ctl "show"). */
static void summon(SDL_Window *win)
{
    SDL_RestoreWindow(win);
    SDL_RaiseWindow(win);
    HWND w = FindWindowA(NULL, WIN_TITLE);
    if (w) SetForegroundWindow(w);
}

/* If the child exited, clean up and take the screen back. */
static void reap_child(SDL_Window *win)
{
    if (!child || WaitForSingleObject(child, 0) != WAIT_OBJECT_0) return;
    CloseHandle(child);
    child = NULL;
    SDL_Log("child exited; reclaiming screen");
    if (audio_ok) Mix_ResumeMusic();
    summon(win);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);  /* drop input queued while away */
}

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
    /* Keep pad input flowing while minimized behind a game — SELECT must
     * work as a global "home" button. */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK |
                 SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);
    SDL_JoystickEventState(SDL_ENABLE);

    char dir[512];
    exe_dir(dir, sizeof dir);
    {
        char lp[600];
        SDL_snprintf(lp, sizeof lp, "%s\\launcher.log", dir);
        logf = fopen(lp, "w");
        SDL_LogSetOutputFunction(log_to_file, NULL);
    }

    /* xp-pad mapping (padwiz): with it the pads become real GameControllers
     * with PS-correct buttons (A=cross accept, B=circle back). The raw
     * joystick path below stays as fallback when no mapping matches. */
    int nmaps = SDL_GameControllerAddMappingsFromFile(
        "C:\\XP_Share\\gamecontrollerdb.txt");
    SDL_Log("gamecontrollerdb: %d mapping(s)", nmaps);
    SDL_Log("%d joystick device(s) at startup", SDL_NumJoysticks());
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        open_pad(i);

    SDL_Window *win = SDL_CreateWindow(WIN_TITLE,
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
        SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : NULL;
    if (!ren) {
        SDL_Log("window/renderer: %s", SDL_GetError());
        return 1;
    }

    /* Native handle for foreground checks (see the input gate below). */
    HWND our_hwnd = NULL;
    {
        SDL_SysWMinfo wm;
        SDL_VERSION(&wm.version);
        if (SDL_GetWindowWMInfo(win, &wm))
            our_hwnd = wm.info.win.window;
    }

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
    for (int i = 0; i < n_movies; i++) {
        SDL_snprintf(name, sizeof name, "movie_%d.bmp", i);
        tex_movie[i] = load_bmp(ren, dir, name);
    }
    for (int i = 0; i < n_songs; i++) {
        SDL_snprintf(name, sizeof name, "song_%d.bmp", i);
        tex_song[i] = load_bmp(ren, dir, name);
    }
    tex_launch = load_bmp(ren, dir, "launch.bmp");

    /* Audio: never fatal — if the device won't open, run silent. */
    if (Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 1024) == 0) {
        audio_ok = 1;
        sfx_move   = load_sfx(dir, "move.wav");
        sfx_select = load_sfx(dir, "select.wav");
        sfx_back   = load_sfx(dir, "back.wav");
        sfx_launch = load_sfx(dir, "launch.wav");
        char mpath[600];
        SDL_snprintf(mpath, sizeof mpath, "%s\\assets\\snd\\ambient.wav", dir);
        bgm = Mix_LoadMUS(mpath);
        if (bgm) {
            Mix_VolumeMusic(MIX_MAX_VOLUME * 40 / 100);   /* soft bed */
            Mix_PlayMusic(bgm, -1);
        }
    } else {
        SDL_Log("Mix_OpenAudio: %s (running silent)", Mix_GetError());
    }

    enum { HOME, LIST, MLIST, SLIST } screen = HOME;
    int sel_home = 0, sel_list = 0, sel_mov = 0, sel_song = 0;
    int running = 1, axis_latch = 0;

    int ctl_tick = 0;
    while (running) {
        int nav_x = 0, nav_y = 0, accept = 0, back = 0, quit = 0, show = 0;
        /* ctl remote intents, kept separate: they're trusted automation and
         * bypass the foreground gate that (rightly) suppresses stray pad
         * input from the background. Merged in after the gate. */
        int cx = 0, cy = 0, cacc = 0, cbk = 0, csh = 0;

        reap_child(win);

        if (++ctl_tick >= 6) {          /* ~5x/sec is plenty */
            ctl_tick = 0;
            poll_ctl(dir, &cx, &cy, &cacc, &cbk, &quit, &csh);
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
            case SDL_CONTROLLERBUTTONDOWN:
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  nav_x = -1; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: nav_x = 1; break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:    nav_y = -1; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  nav_y = 1; break;
                case SDL_CONTROLLER_BUTTON_A:                     /* cross */
                case SDL_CONTROLLER_BUTTON_START:      accept = 1; break;
                case SDL_CONTROLLER_BUTTON_B:          back = 1; break;
                case SDL_CONTROLLER_BUTTON_BACK:       show = 1; break; /* SELECT */
                default: break;
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (e.caxis.axis > SDL_CONTROLLER_AXIS_LEFTY) break;
                if (e.caxis.value < -16000 && !axis_latch) {
                    if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
                        nav_x = -1;
                    else
                        nav_y = -1;
                    axis_latch = 1;
                } else if (e.caxis.value > 16000 && !axis_latch) {
                    if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
                        nav_x = 1;
                    else
                        nav_y = 1;
                    axis_latch = 1;
                } else if (e.caxis.value > -8000 && e.caxis.value < 8000) {
                    axis_latch = 0;
                }
                break;
            case SDL_JOYDEVICEADDED:      /* hotplug: pads can join anytime */
                if (!pad_known(SDL_JoystickGetDeviceInstanceID(e.jdevice.which)))
                    open_pad(e.jdevice.which);
                break;
            case SDL_JOYDEVICEREMOVED:
                close_pad(e.jdevice.which);
                break;
            case SDL_JOYHATMOTION:
                if (is_mapped(e.jhat.which)) break;  /* controller path handles it */
                if (e.jhat.value & SDL_HAT_LEFT)  nav_x = -1;
                if (e.jhat.value & SDL_HAT_RIGHT) nav_x = 1;
                if (e.jhat.value & SDL_HAT_UP)    nav_y = -1;
                if (e.jhat.value & SDL_HAT_DOWN)  nav_y = 1;
                break;
            case SDL_JOYAXISMOTION:
                if (is_mapped(e.jaxis.which)) break;
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
                if (is_mapped(e.jbutton.which)) break;
                if (e.jbutton.button == 0) accept = 1;   /* A */
                if (e.jbutton.button == 1) back = 1;     /* B */
                if (e.jbutton.button == 8) show = 1;     /* SELECT (typical) */
                break;
            }
        }

        show |= csh;
        if (show) summon(win);

        /* Only act on PHYSICAL navigation / launching when we are actually
         * the foreground window. We keep reading the gamepad in the
         * background (SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS) ONLY so
         * SELECT can summon us back — but without this gate, every other
         * button leaks through while a game is running and launches things
         * behind it. A game we launched sets `child`; but a game started
         * outside the launcher (or any other foreground app) must suppress
         * us too, so gate on the real foreground state, not just `child`. */
        if (GetForegroundWindow() != our_hwnd)
            nav_x = nav_y = accept = back = 0;

        /* ctl remote is exempt from the gate (trusted, local-only). */
        if (!nav_x) nav_x = cx;
        if (!nav_y) nav_y = cy;
        accept |= cacc;
        back |= cbk;

        if (nav_x || nav_y) sfx(sfx_move);

        if (screen == HOME) {
            if (nav_x < 0 && sel_home > 0) sel_home--;
            if (nav_x > 0 && sel_home < 2) sel_home++;
            if (back) running = 0;
            if (accept) {
                if (sel_home == 0 && n_games > 0) {
                    sfx(sfx_select); screen = LIST; sel_list = 0;
                }
                /* A section with scanned media opens a browse list; with
                 * none, fall back to just launching the player. */
                else if (sel_home == 1) {
                    if (n_songs > 0) { sfx(sfx_select); screen = SLIST; sel_song = 0; }
                    else launch(win, ren, &music);
                }
                else if (sel_home == 2) {
                    if (n_movies > 0) { sfx(sfx_select); screen = MLIST; sel_mov = 0; }
                    else launch(win, ren, &movies);
                }
            }
        } else if (screen == LIST) {
            if (nav_y < 0 && sel_list > 0) sel_list--;
            if (nav_y > 0 && sel_list < n_games - 1) sel_list++;
            if (back) { sfx(sfx_back); screen = HOME; }
            if (accept) launch(win, ren, &games[sel_list]);
        } else if (screen == MLIST) {
            if (nav_y < 0 && sel_mov > 0) sel_mov--;
            if (nav_y > 0 && sel_mov < n_movies - 1) sel_mov++;
            if (back) { sfx(sfx_back); screen = HOME; }
            if (accept) launch(win, ren, &movie_items[sel_mov]);
        } else { /* SLIST */
            if (nav_y < 0 && sel_song > 0) sel_song--;
            if (nav_y > 0 && sel_song < n_songs - 1) sel_song++;
            if (back) { sfx(sfx_back); screen = HOME; }
            if (accept) launch(win, ren, &song_items[sel_song]);
        }

        SDL_Texture *t = tex_home[sel_home];
        if (screen == LIST)  t = tex_list[sel_list];
        else if (screen == MLIST) t = tex_movie[sel_mov];
        else if (screen == SLIST) t = tex_song[sel_song];
        if (t) SDL_RenderCopy(ren, t, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(33);   /* ~30fps is plenty for a menu; be kind to the Atom */
    }

    for (int i = 0; i < n_pads; i++) SDL_GameControllerClose(pads[i]);
    for (int i = 0; i < n_raw; i++) SDL_JoystickClose(raw_joys[i]);
    if (audio_ok) {
        if (bgm) Mix_FreeMusic(bgm);
        Mix_FreeChunk(sfx_move); Mix_FreeChunk(sfx_select);
        Mix_FreeChunk(sfx_back); Mix_FreeChunk(sfx_launch);
        Mix_CloseAudio();
    }
    if (logf) fclose(logf);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
