# xp-launcher — cross-compiled from Linux for Windows XP (32-bit).
# Needs: gcc-mingw-w64-i686, vendored SDL2 (vendor/SDL2), ImageMagick (assets).

CC      = i686-w64-mingw32-gcc
SDL     = vendor/SDL2
CFLAGS  = -O2 -Wall -Wextra -I$(SDL)/include/SDL2 -Dmain=SDL_main
LDFLAGS = -L$(SDL)/lib -lmingw32 -lSDL2main -lSDL2 -mwindows -static-libgcc

BUILD   = build
DEPLOY  = /media/Acer_Notebook/launcher

all: $(BUILD)/launcher.exe assets

$(BUILD)/launcher.exe: src/main.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

assets:
	python3 scripts/gen-assets.py

# Copy exe + SDL2.dll + assets onto the XP box via the SMB share.
# Kills a running instance first: XP holds the exe/dll locked while running.
deploy: all kill
	mkdir -p $(DEPLOY)/assets
	cp $(BUILD)/launcher.exe $(SDL)/bin/SDL2.dll $(DEPLOY)/
	cp assets/*.bmp $(DEPLOY)/assets/

# Launch on the box (lands on the TV) and grab a screenshot to verify.
run:
	~/bin/xprun 'start "" "C:\XP_Share\launcher\launcher.exe"'
	sleep 3 && ~/bin/xpshot /tmp/launcher-shot.png

kill:
	-~/bin/xprun 'taskkill /f /im launcher.exe' >/dev/null 2>&1

clean:
	rm -rf $(BUILD)

.PHONY: all assets deploy run kill clean
