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
#include <SDL_image.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GAMES 64
#define MAX_MEDIA 128
#define ATTRACT_IDLE  45000    /* ms of no input before the attract loop */
#define ATTRACT_CYCLE  6000    /* ms per attract frame */
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
static SDL_Texture *tex_np[MAX_MEDIA];   /* now-playing, per song */
static SDL_Texture *tex_attract[MAX_GAMES];  /* idle attract loop, per game */
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
    SDL_Surface *s = IMG_Load(path);   /* screens are PNG now (~6x smaller) */
    if (!s) {
        SDL_Log("load_img %s: %s", path, IMG_GetError());
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
                     int *accept, int *back, int *quit, int *show,
                     int *shuf, int *rep, int *skl, int *skr)
{
    char path[600];
    SDL_snprintf(path, sizeof path, "%s\\ctl.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char cmd[64] = "";
    if (fgets(cmd, sizeof cmd, f))
        cmd[strcspn(cmd, "\r\n")] = '\0';
    fclose(f);
    if (!cmd[0]) return;              /* already-consumed (empty) file */
    /* Consume by truncating to empty rather than remove(): over the SMB
     * share, delete+recreate churns the directory-entry cache and the box
     * intermittently misses the next command. Keeping the file present and
     * just emptying it is coherent. */
    FILE *w = fopen(path, "w");
    if (w) fclose(w);
    if      (strcmp(cmd, "left") == 0)  *nav_x = -1;
    else if (strcmp(cmd, "right") == 0) *nav_x = 1;
    else if (strcmp(cmd, "up") == 0)    *nav_y = -1;
    else if (strcmp(cmd, "down") == 0)  *nav_y = 1;
    else if (strcmp(cmd, "enter") == 0) *accept = 1;
    else if (strcmp(cmd, "back") == 0)  *back = 1;
    else if (strcmp(cmd, "quit") == 0)  *quit = 1;
    else if (strcmp(cmd, "show") == 0)  *show = 1;
    else if (strcmp(cmd, "shuffle") == 0) *shuf = 1;
    else if (strcmp(cmd, "repeat") == 0)  *rep = 1;
    else if (strcmp(cmd, "seekl") == 0)   *skl = 1;
    else if (strcmp(cmd, "seekr") == 0)   *skr = 1;
}

/* UI sound. Subtle blips + a quiet ambient bed (see scripts/gen-sounds.py).
 * audio_ok stays 0 if the box has no working audio device — everything
 * degrades to silent, never fatal. */
static int audio_ok;
static Mix_Chunk *sfx_move, *sfx_select, *sfx_back, *sfx_launch;
static Mix_Music *bgm;                   /* ambient bed */
static Mix_Music *track;                 /* the song currently playing */
static int cur_song = -1;                /* index into song_items, -1 = none */
static int mus_paused = 0;
static int mus_shuffle = 0;              /* random next track */
static int mus_repeat = 0;              /* 0 off, 1 all, 2 one */
static int mus_vol = 70;                 /* 0-100 */
static Uint32 play_start, pause_at, game_pause_at;   /* progress bookkeeping */

/* Now-Playing layout, mirrored from scripts/gen-assets.py (logical 1024x768).
 * The engine draws the live fills / glyphs at these coords over the
 * pre-rendered screen. Keep in sync with gen-assets PROG/VOL/STATE/FBTN. */
#define PROG_X 200
#define PROG_Y 452
#define PROG_W 624
#define PROG_H 8
#define VOL_X 470
#define VOL_Y 489
#define VOL_W 120
#define VOL_H 8
#define STATE_X 176
#define STATE_Y 456
#define FB_TRI_X 512   /* repeat  */
#define FB_TRI_Y 556
#define FB_SQR_X 464   /* shuffle */
#define FB_SQR_Y 592
#define FB_R 21

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

/* song_items[i].args holds the file path, quoted (for the shell-out path we
 * no longer use). Native playback wants the bare path. */
static void unquote(char *dst, size_t n, const char *src)
{
    size_t len = strlen(src);
    if (len >= 2 && src[0] == '"' && src[len - 1] == '"') {
        size_t m = len - 2;
        if (m >= n) m = n - 1;
        memcpy(dst, src + 1, m);
        dst[m] = '\0';
    } else {
        SDL_strlcpy(dst, src, n);
    }
}

/* Play song `idx` in-process (replaces the ambient bed / previous track).
 * The launcher keeps running, so playback continues while you browse. */
static void play_song(int idx)
{
    if (!audio_ok || idx < 0 || idx >= n_songs) return;
    char path[600];
    unquote(path, sizeof path, song_items[idx].args);
    Mix_HaltMusic();
    if (track) { Mix_FreeMusic(track); track = NULL; }
    track = Mix_LoadMUS(path);
    if (!track) {
        SDL_Log("play_song %s: %s", path, Mix_GetError());
        cur_song = -1;
        return;
    }
    Mix_VolumeMusic(MIX_MAX_VOLUME * mus_vol / 100);
    Mix_PlayMusic(track, 1);            /* once; auto-advance handles the rest */
    cur_song = idx;
    mus_paused = 0;
    play_start = SDL_GetTicks();
}

static void change_vol(int delta)
{
    mus_vol += delta;
    if (mus_vol < 0) mus_vol = 0;
    if (mus_vol > 100) mus_vol = 100;
    if (audio_ok) Mix_VolumeMusic(MIX_MAX_VOLUME * mus_vol / 100);
}

static double song_pos(void);  /* fwd */

/* Jump within the current track (best effort — works for MP3/OGG/FLAC;
 * WAV may not support it, in which case leave the estimate untouched). */
static void do_seek(double delta)
{
    if (!audio_ok || cur_song < 0 || !track) return;
    double p = song_pos() + delta;
    if (p < 0) p = 0;
    if (Mix_SetMusicPosition(p) == 0)          /* success -> match estimate */
        play_start = SDL_GetTicks() - (Uint32)(p * 1000);
}

/* Choose the next track for auto-advance / Next, honoring shuffle + repeat. */
static int next_track(void)
{
    if (n_songs <= 1) return cur_song;
    if (mus_shuffle) {
        int r;
        do { r = rand() % n_songs; } while (r == cur_song);
        return r;
    }
    return (cur_song + 1) % n_songs;
}

static void toggle_pause(void)
{
    if (!audio_ok || cur_song < 0) return;
    if (mus_paused) {
        play_start += SDL_GetTicks() - pause_at;   /* keep progress honest */
        Mix_ResumeMusic();
        mus_paused = 0;
    } else {
        pause_at = SDL_GetTicks();
        Mix_PauseMusic();
        mus_paused = 1;
    }
}

/* Elapsed seconds into the current track (pause-aware, format-independent). */
static double song_pos(void)
{
    Uint32 now = mus_paused ? pause_at : SDL_GetTicks();
    return (now - play_start) / 1000.0;
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
    if (audio_ok) { game_pause_at = SDL_GetTicks(); Mix_PauseMusic(); }
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
    if (audio_ok) {
        if (cur_song >= 0 && !mus_paused)
            play_start += SDL_GetTicks() - game_pause_at;   /* don't count game time */
        Mix_ResumeMusic();
    }
    summon(win);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);  /* drop input queued while away */
}

/* Filled triangle in the current logical space (for the play glyph and the
 * repeat indicator) — the software renderer has no triangle primitive. */
static void fill_tri(SDL_Renderer *ren, float x1, float y1, float x2, float y2,
                     float x3, float y3, Uint8 r, Uint8 g, Uint8 b)
{
    SDL_Color c = { r, g, b, 255 };
    SDL_Vertex v[3] = { { { x1, y1 }, c, { 0, 0 } },
                        { { x2, y2 }, c, { 0, 0 } },
                        { { x3, y3 }, c, { 0, 0 } } };
    SDL_RenderGeometry(ren, NULL, v, 3, NULL, 0);
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
    /* Render in a fixed 1024x768 logical space, scaled to whatever the TV
     * is at. Keeps the pre-rendered BMPs and the live overlays (progress
     * bar) aligned regardless of the actual desktop resolution. */
    SDL_RenderSetLogicalSize(ren, 1024, 768);

    /* Native handle for foreground checks (see the input gate below). */
    HWND our_hwnd = NULL;
    {
        SDL_SysWMinfo wm;
        SDL_VERSION(&wm.version);
        if (SDL_GetWindowWMInfo(win, &wm))
            our_hwnd = wm.info.win.window;
    }

    load_cfg(dir);
    srand(SDL_GetTicks());              /* for shuffle */

    char name[64];
    for (int i = 0; i < 3; i++) {
        SDL_snprintf(name, sizeof name, "home_%d.png", i);
        tex_home[i] = load_bmp(ren, dir, name);
    }
    for (int i = 0; i < n_games; i++) {
        SDL_snprintf(name, sizeof name, "list_%d.png", i);
        tex_list[i] = load_bmp(ren, dir, name);
        SDL_snprintf(name, sizeof name, "attract_%d.png", i);
        tex_attract[i] = load_bmp(ren, dir, name);
    }
    for (int i = 0; i < n_movies; i++) {
        SDL_snprintf(name, sizeof name, "movie_%d.png", i);
        tex_movie[i] = load_bmp(ren, dir, name);
    }
    for (int i = 0; i < n_songs; i++) {
        SDL_snprintf(name, sizeof name, "song_%d.png", i);
        tex_song[i] = load_bmp(ren, dir, name);
        SDL_snprintf(name, sizeof name, "np_%d.png", i);
        tex_np[i] = load_bmp(ren, dir, name);
    }
    tex_launch = load_bmp(ren, dir, "launch.png");

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

    enum { HOME, LIST, MLIST, SLIST, NOWPLAYING } screen = HOME;
    int sel_home = 0, sel_list = 0, sel_mov = 0, sel_song = 0;
    int running = 1, axis_latch = 0;
    Uint32 last_input = SDL_GetTicks();    /* attract-mode idle timer */
    int attract = 0, attract_idx = 0;
    Uint32 attract_cycle_at = 0;

    int ctl_tick = 0;
    while (running) {
        int nav_x = 0, nav_y = 0, accept = 0, back = 0, quit = 0, show = 0;
        int m_shuf = 0, m_rep = 0, m_skl = 0, m_skr = 0;   /* music actions */
        /* ctl remote intents, kept separate: they're trusted automation and
         * bypass the foreground gate that (rightly) suppresses stray pad
         * input from the background. Merged in after the gate. */
        int cx = 0, cy = 0, cacc = 0, cbk = 0, csh = 0;
        int c_shuf = 0, c_rep = 0, c_skl = 0, c_skr = 0;

        reap_child(win);

        if (++ctl_tick >= 6) {          /* ~5x/sec is plenty */
            ctl_tick = 0;
            poll_ctl(dir, &cx, &cy, &cacc, &cbk, &quit, &csh,
                     &c_shuf, &c_rep, &c_skl, &c_skr);
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
                case SDL_CONTROLLER_BUTTON_X:          m_shuf = 1; break; /* square */
                case SDL_CONTROLLER_BUTTON_Y:          m_rep = 1; break;  /* triangle */
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  m_skl = 1; break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: m_skr = 1; break;
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
                /* raw fallback (padwiz layout): b2 cross, b1 circle, b0
                 * triangle, b3 square, b4/b5 shoulders, b8 select */
                if (e.jbutton.button == 2) accept = 1;   /* cross */
                if (e.jbutton.button == 1) back = 1;     /* circle */
                if (e.jbutton.button == 3) m_shuf = 1;   /* square */
                if (e.jbutton.button == 0) m_rep = 1;    /* triangle */
                if (e.jbutton.button == 4) m_skl = 1;    /* L1 */
                if (e.jbutton.button == 5) m_skr = 1;    /* R1 */
                if (e.jbutton.button == 8) show = 1;     /* SELECT */
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
            nav_x = nav_y = accept = back = m_shuf = m_rep = m_skl = m_skr = 0;

        /* ctl remote is exempt from the gate (trusted, local-only). */
        if (!nav_x) nav_x = cx;
        if (!nav_y) nav_y = cy;
        accept |= cacc;
        back |= cbk;
        m_shuf |= c_shuf; m_rep |= c_rep; m_skl |= c_skl; m_skr |= c_skr;

        /* Attract mode: after a while idle, cycle the library as an arcade
         * attract loop. The first input just wakes us (it doesn't also
         * navigate); music, if any, keeps playing underneath. */
        {
            Uint32 now = SDL_GetTicks();
            int any_input = nav_x || nav_y || accept || back || show
                          || m_shuf || m_rep || m_skl || m_skr;
            if (any_input) last_input = now;
            if (attract) {
                if (any_input) {
                    attract = 0;
                    nav_x = nav_y = accept = back = 0;   /* consume the wake */
                    m_shuf = m_rep = m_skl = m_skr = 0;
                } else if (now - attract_cycle_at > ATTRACT_CYCLE && n_games) {
                    attract_idx = (attract_idx + 1) % n_games;
                    attract_cycle_at = now;
                }
            } else if (n_games > 0 && now - last_input > ATTRACT_IDLE) {
                attract = 1; attract_idx = 0; attract_cycle_at = now;
            }
        }

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
        } else if (screen == SLIST) {
            if (nav_y < 0 && sel_song > 0) sel_song--;
            if (nav_y > 0 && sel_song < n_songs - 1) sel_song++;
            if (back) { sfx(sfx_back); screen = HOME; }
            if (accept) {           /* play in-process, go to Now Playing */
                sfx(sfx_select);
                play_song(sel_song);
                screen = NOWPLAYING;
            }
        } else { /* NOWPLAYING */
            if (nav_x < 0) play_song((cur_song - 1 + n_songs) % n_songs); /* prev */
            if (nav_x > 0) play_song(next_track());                       /* next */
            if (nav_y < 0) change_vol(+5);      /* d-pad up   = louder */
            if (nav_y > 0) change_vol(-5);      /* d-pad down = quieter */
            if (accept) toggle_pause();         /* ✕ */
            if (m_shuf) { mus_shuffle = !mus_shuffle; sfx(sfx_select); }   /* □ */
            if (m_rep)  { mus_repeat = (mus_repeat + 1) % 3; sfx(sfx_select); } /* △ */
            if (m_skl) do_seek(-10);            /* L1 */
            if (m_skr) do_seek(+10);            /* R1 */
            if (back) { sfx(sfx_back); screen = SLIST; }  /* ○ music keeps going */
        }

        /* Auto-advance / repeat / stop-at-end when a track finishes. */
        if (audio_ok && cur_song >= 0 && !mus_paused && !Mix_PlayingMusic()) {
            if (mus_repeat == 2)                        /* repeat one */
                play_song(cur_song);
            else if (!mus_shuffle && cur_song == n_songs - 1 && mus_repeat == 0)
                mus_paused = 1;                         /* end of list, stop */
            else
                play_song(next_track());
        }

        SDL_Texture *t = tex_home[sel_home];
        if (screen == LIST)  t = tex_list[sel_list];
        else if (screen == MLIST) t = tex_movie[sel_mov];
        else if (screen == SLIST) t = tex_song[sel_song];
        else if (screen == NOWPLAYING && cur_song >= 0) t = tex_np[cur_song];
        if (attract && n_games > 0 && tex_attract[attract_idx])
            t = tex_attract[attract_idx];
        if (t) SDL_RenderCopy(ren, t, NULL, NULL);

        /* Live Now-Playing overlays (coords mirror gen-assets, logical space):
         * progress fill + playhead, volume meter, play/pause state glyph,
         * and shuffle/repeat active indicators on their face buttons. */
        if (!attract && screen == NOWPLAYING && cur_song >= 0) {
            if (track) {
                double dur = Mix_MusicDuration(track);
                double frac = dur > 0 ? song_pos() / dur : 0;
                if (frac < 0) frac = 0;
                if (frac > 1) frac = 1;
                int w = (int)(PROG_W * frac);
                SDL_SetRenderDrawColor(ren, 86, 156, 214, 255);
                SDL_Rect pf = { PROG_X, PROG_Y, w, PROG_H };
                SDL_RenderFillRect(ren, &pf);
                SDL_SetRenderDrawColor(ren, 235, 238, 245, 255);
                SDL_Rect knob = { PROG_X + w - 3, PROG_Y - 6, 6, PROG_H + 12 };
                SDL_RenderFillRect(ren, &knob);
            }
            /* volume meter fill */
            SDL_SetRenderDrawColor(ren, 120, 200, 235, 255);
            SDL_Rect vf = { VOL_X, VOL_Y, VOL_W * mus_vol / 100, VOL_H };
            SDL_RenderFillRect(ren, &vf);
            /* play/pause state glyph (▶ playing / ❚❚ paused) */
            if (mus_paused) {
                SDL_SetRenderDrawColor(ren, 232, 176, 74, 255);
                SDL_Rect b1 = { STATE_X - 6, STATE_Y - 8, 4, 16 };
                SDL_Rect b2 = { STATE_X + 2, STATE_Y - 8, 4, 16 };
                SDL_RenderFillRect(ren, &b1);
                SDL_RenderFillRect(ren, &b2);
            } else {
                fill_tri(ren, STATE_X - 5, STATE_Y - 8, STATE_X - 5,
                         STATE_Y + 8, STATE_X + 8, STATE_Y, 120, 200, 235);
            }
            /* shuffle on: filled pink square on the □ button */
            if (mus_shuffle) {
                SDL_SetRenderDrawColor(ren, 214, 100, 168, 255);
                SDL_Rect sq = { FB_SQR_X - 10, FB_SQR_Y - 10, 20, 20 };
                SDL_RenderFillRect(ren, &sq);
            }
            /* repeat on: filled green triangle on the △ button (+ dot for one) */
            if (mus_repeat) {
                fill_tri(ren, FB_TRI_X, FB_TRI_Y - 11.0f,
                         FB_TRI_X - 10.0f, FB_TRI_Y + 8.0f,
                         FB_TRI_X + 10.0f, FB_TRI_Y + 8.0f, 77, 190, 122);
                if (mus_repeat == 2) {
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                    SDL_Rect dot = { FB_TRI_X - 2, FB_TRI_Y - 1, 4, 4 };
                    SDL_RenderFillRect(ren, &dot);
                }
            }
        }
        SDL_RenderPresent(ren);
        SDL_Delay(33);   /* ~30fps is plenty for a menu; be kind to the Atom */
    }

    for (int i = 0; i < n_pads; i++) SDL_GameControllerClose(pads[i]);
    for (int i = 0; i < n_raw; i++) SDL_JoystickClose(raw_joys[i]);
    if (audio_ok) {
        Mix_HaltMusic();
        if (track) Mix_FreeMusic(track);
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
