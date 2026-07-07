# xp-launcher — cross-compiled from Linux for Windows XP (32-bit).
# Needs: gcc-mingw-w64-i686, vendored SDL2 (vendor/SDL2), ImageMagick (assets).

CC      = i686-w64-mingw32-gcc
SDL     = vendor/SDL2
CFLAGS  = -O2 -Wall -Wextra -I$(SDL)/include/SDL2 -Dmain=SDL_main
LDFLAGS = -L$(SDL)/lib -lmingw32 -lSDL2main -lSDL2 -lSDL2_mixer -mwindows -static-libgcc

BUILD   = build
DEPLOY  = /media/Acer_Notebook/launcher

all: $(BUILD)/launcher.exe assets

$(BUILD)/launcher.exe: src/main.c $(BUILD)/icon.o
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ src/main.c $(BUILD)/icon.o $(LDFLAGS)

$(BUILD)/icon.o: src/launcher.rc src/launcher.ico
	@mkdir -p $(BUILD)
	i686-w64-mingw32-windres --include-dir src -i src/launcher.rc -o $@

assets:
	python3 scripts/gen-assets.py
	python3 scripts/gen-sounds.py

# Copy exe + SDL2.dll + assets onto the XP box via the SMB share.
# Kills a running instance first: XP holds the exe/dll locked while running.
deploy: all kill
	mkdir -p $(DEPLOY)/assets/snd
	cp $(BUILD)/launcher.exe $(SDL)/bin/SDL2.dll $(SDL)/bin/SDL2_mixer.dll $(DEPLOY)/
	rm -f $(DEPLOY)/assets/*.bmp $(DEPLOY)/assets/games.cfg
	cp assets/*.bmp assets/games.cfg $(DEPLOY)/assets/
	cp assets/snd/*.wav $(DEPLOY)/assets/snd/

# Launch on the box (lands on the TV) and grab a screenshot to verify.
run:
	~/bin/xprun 'start "" "C:\XP_Share\launcher\launcher.exe"'
	sleep 3 && ~/bin/xpshot /tmp/launcher-shot.png

kill:
	-~/bin/xprun 'taskkill /f /im launcher.exe' >/dev/null 2>&1

clean:
	rm -rf $(BUILD)

.PHONY: all assets deploy run kill clean shortcuts deploy-media

# One-time install: desktop/start-menu/startup shortcuts on the box.
shortcuts:
	cp scripts/mklnk.vbs /media/Acer_Notebook/
	~/bin/xprun 'cscript //nologo c:\XP_Share\mklnk.vbs'

# Push staged sample media (incl. a real MP3) to the box's media folders.
# Run when you're home and the share is mounted.
deploy-media:
	mkdir -p /media/Acer_Notebook/media/music /media/Acer_Notebook/media/movies
	-cp media-samples/music/* /media/Acer_Notebook/media/music/ 2>/dev/null
	-cp media-samples/movies/* /media/Acer_Notebook/media/movies/ 2>/dev/null
	@echo "media deployed — re-run 'make deploy' to regenerate the music list"
