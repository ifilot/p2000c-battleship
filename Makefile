# Zeeslag (battleship) for the Philips P2000C: C (Z88DK/sdcc) + Z80 assembly, CP/M target.
#
# The compiler runs in the z88dk/z88dk Docker image. Screenshots, tests and
# `make run` need the sibling p2000c-cpm-disk-tool checkout (headless emulator
# and dist/pro/ disk images) and, for the character-ROM font, p2000c-emulator.

VERSION    = 1.0.0
BUILD_DATE = $(shell date +%Y-%m-%d)

# -SO2, not -SO3: the level-3 peephole rules dropped the stores of `x |= flag`
# and the loads of an array comparison in this code (seen in the listings).
ZCC      = docker run --rm --user $(shell id -u):$(shell id -g) -v "$(CURDIR)":/src -w /src z88dk/z88dk zcc
ZCCFLAGS = +cpm -vn -clib=sdcc_iy -O3 -SO2 --opt-code-speed --max-allocs-per-node200000 \
           -Ibuild -create-app -m

SOURCES = src/main.c src/game.c src/screen.c src/panel.c src/screens.c src/saver.c src/clock.c src/fleet.c src/cpu.c src/video.asm
HEADERS = src/video.h src/fleet.h src/cpu.h src/game.h src/screen.h src/panel.h src/screens.h src/saver.h src/clock.h src/sprites.h src/splash.h src/version.h
COM     = build/ZEESLAG.COM

# Deployment image for the SASI emulator (ZuluBlaster): a second-disk image
# with the standard split layout (E: low, F: high) built with the sibling
# disk tool's CLI and its split system tracks, holding only the game on F:.
DISKTOOL      = ../p2000c-cpm-disk-tool
P2000C_DISK   = PYTHONPATH=$(DISKTOOL)/src python3 -m p2000c_disk.cli
SYSTEM_TRACKS = $(DISKTOOL)/assets/boot/hdboot-split.trk
DEPLOY_IMAGE  = build/HD1_256.hda

.PHONY: all build run screenshot test sprites deploy clean

all: build

build: $(COM)

$(COM): $(SOURCES) $(HEADERS) Makefile
	mkdir -p build
	printf '#define VERSION "%s"\n#define BUILD_DATE "%s"\n' "$(VERSION)" "$(BUILD_DATE)" > build/build_info.h
	$(ZCC) $(ZCCFLAGS) $(SOURCES) -o build/zeeslag
	rm -f build/zeeslag build/zeeslag_CODE.bin

# HD1_256.hda with ZEESLAG.COM on F: and nothing else. Copy it to the SD card
# in place of the distribution's HD1_256.hda.
deploy: build
	rm -f $(DEPLOY_IMAGE)
	$(P2000C_DISK) build $(DEPLOY_IMAGE) --layout split --system $(SYSTEM_TRACKS)
	$(P2000C_DISK) put-many $(DEPLOY_IMAGE) $(COM) --partition high
	$(P2000C_DISK) verify $(DEPLOY_IMAGE)
	$(P2000C_DISK) list $(DEPLOY_IMAGE)

# Regenerate the tiles, ship pictures and glyphs, and the title picture
# (need the p2000c-emulator font sheet). --preview writes build/sprites_preview.png.
sprites:
	python3 tools/gen_sprites.py --preview
	python3 tools/gen_splash.py

# Open the game in the graphical emulator.
run: build
	python3 tools/run.py

# Plain raster screenshots of the placement and battle screens -> build/*.png
screenshot: build
	python3 tools/render.py

# Full games against the computer in the headless emulator.
test: build
	python3 tools/test_game.py

clean:
	rm -rf build
