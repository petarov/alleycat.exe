# Alley Cat port: CAT.EXE code translated to x86-64 (generated from the Ghidra
# listing re/CAT.asm) + emulated BIOS/hardware + SDL2 front end.
# Builds an x86_64 binary: on macOS it runs under Rosetta 2 on Apple Silicon,
# on Linux it needs an x86_64 host and the system SDL2 (pkg-config sdl2),
# on Windows SDL2.dll next to alleycat.exe.
# The web build (make web, in Docker) translates the listing to C instead
# (tools/asm2c.py) and compiles it to WebAssembly with Emscripten.

BUILD    := build
LISTING  := ../re/CAT.asm
EXE      := ../CAT.EXE
NASM     := nasm
PYTHON   := python3 -I
VER      := 1.3

# Windows is cross-compiled with mingw-w64 (make windows, in Docker).
TARGET_OS ?= $(shell uname -s)

ifeq ($(TARGET_OS),Windows)

SDL_MINGW ?= /opt/SDL2/x86_64-w64-mingw32
CC       := x86_64-w64-mingw32-gcc
CFLAGS   := -std=c11 -O2 -Wall -Wextra -Isrc -I$(SDL_MINGW)/include
NFLAGS   := -f win64 -Wall -w-reloc-rel-dword -Isrc/
LDFLAGS  := -L$(SDL_MINGW)/lib -lmingw32 -lSDL2main -lSDL2 -static-libgcc
# The game is a GUI program (no console window); SDL2main supplies WinMain.
GUI_LDFLAGS := -mwindows
EXE_SUFFIX  := .exe

else ifeq ($(TARGET_OS),Web)

CC       := emcc
CFLAGS   := -std=c11 -O2 -Wall -Wextra -Isrc -sUSE_SDL=2
WEB_LDFLAGS := -O2 -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH -sENVIRONMENT=web

else ifeq ($(TARGET_OS),Linux)

CC       := cc
CFLAGS   := -std=c11 -O2 -Wall -Wextra -Isrc $(shell pkg-config --cflags sdl2)
NFLAGS   := -f elf64 -Wall -w-reloc-rel-dword -Isrc/
LDFLAGS  := $(shell pkg-config --libs sdl2)

else

ARCH     := x86_64
SDL_DIR  := third_party
CC       := clang
CFLAGS   := -arch $(ARCH) -std=c11 -O2 -Wall -Wextra -F$(SDL_DIR) -Isrc
# Mach-O C symbols carry a leading underscore; --gprefix adds it to asm globals/externs.
# RIP-relative references across sections are normal on Mach-O, so that warning is off.
NFLAGS   := -f macho64 --gprefix _ -Wall -w-reloc-rel-dword -Isrc/
LDFLAGS  := -arch $(ARCH) -F$(SDL_DIR) -framework SDL2 -Wl,-rpath,@executable_path/../$(SDL_DIR) \
            -Wl,-rpath,@executable_path

endif

GAME_OBJS := $(BUILD)/bios.o $(BUILD)/font8x8.o $(BUILD)/speaker.o $(BUILD)/game.o $(BUILD)/cat_code.o $(BUILD)/data.o
TARGET    := $(BUILD)/alleycat$(EXE_SUFFIX)
CHECK     := $(BUILD)/check_title

ifeq ($(TARGET_OS),Web)
all: $(BUILD)/index.html
else ifeq ($(TARGET_OS),Windows)
all: $(TARGET) $(BUILD)/SDL2.dll

$(BUILD)/SDL2.dll: $(SDL_MINGW)/bin/SDL2.dll | $(BUILD)
	cp $< $@
else
all: $(TARGET)
endif

$(TARGET): $(BUILD)/platform.o $(GAME_OBJS)
	$(CC) $^ $(LDFLAGS) $(GUI_LDFLAGS) -o $@

$(BUILD)/cat_code.asm: $(LISTING) tools/asm2nasm.py | $(BUILD)
	$(PYTHON) tools/asm2nasm.py $(LISTING) $@

$(BUILD)/cat_code.o: $(BUILD)/cat_code.asm src/runtime.inc
	$(NASM) $(NFLAGS) $< -o $@

$(BUILD)/game.o: src/game.asm src/runtime.inc | $(BUILD)
	$(NASM) $(NFLAGS) $< -o $@

# data.asm incbins ../CAT.EXE relative to this directory.
$(BUILD)/data.o: src/data.asm src/runtime.inc $(EXE) | $(BUILD)
	$(NASM) $(NFLAGS) $< -o $@

$(BUILD)/%.o: src/%.c src/bios.h src/font8x8.h src/speaker.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Web: the same C sources plus the game translated to C; the page is
# index.html with index.js and index.wasm next to it.
WEB_OBJS := $(BUILD)/platform.o $(BUILD)/bios.o $(BUILD)/font8x8.o $(BUILD)/speaker.o \
            $(BUILD)/runtime.o $(BUILD)/cat_code_c.o

$(BUILD)/index.html: $(WEB_OBJS) src/web/shell.html
	$(CC) $(WEB_OBJS) $(WEB_LDFLAGS) --shell-file src/web/shell.html -o $@

$(BUILD)/cat_code.c: $(LISTING) $(EXE) tools/asm2c.py tools/asm2nasm.py | $(BUILD)
	$(PYTHON) tools/asm2c.py $(LISTING) $(EXE) $@

$(BUILD)/cat_code_c.o: $(BUILD)/cat_code.c src/runtime.h src/bios.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/runtime.o: src/runtime.h

# The version is shown on the title screen; rebuild when VER changes.
$(BUILD)/platform.o: CFLAGS += -DALLEYCAT_VERSION='"$(VER)"'
$(BUILD)/platform.o: Makefile

# Headless check: generated backdrop code vs. the independent Python model.
$(BUILD)/check_main.o: tests/check_main.c src/bios.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/check_title.o: tests/check_title.asm src/runtime.inc | $(BUILD)
	$(NASM) $(NFLAGS) $< -o $@

$(CHECK): $(BUILD)/check_main.o $(BUILD)/check_title.o $(GAME_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

check: $(CHECK)
	./$(CHECK) $(BUILD)/vram_game.bin
	$(PYTHON) tools/ref_title.py $(EXE) $(BUILD)/vram_ref.bin
	cmp $(BUILD)/vram_game.bin $(BUILD)/vram_ref.bin && echo "check: title backdrop matches reference"

# Headless smoke test: runs main (CS:0000) for SMOKE_SECONDS without a window.
SMOKE_SECONDS ?= 4

$(BUILD)/smoke_main.o: tests/smoke_main.c src/bios.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/smoke: $(BUILD)/smoke_main.o $(GAME_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

smoke: $(BUILD)/smoke
	./$(BUILD)/smoke $(BUILD)/vram_smoke.bin $(SMOKE_SECONDS)

# Linux build + headless tests in an x86_64 container (emulated on Apple Silicon).
# The toolchain image is built once and reused; objects go to build/linux.
LINUX_BASE  ?= debian:trixie
LINUX_IMAGE := alleycat-linux-build

linux-image:
	printf 'FROM $(LINUX_BASE)\nRUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq make gcc nasm python3 pkg-config libsdl2-dev >/dev/null\n' \
		| docker build --platform linux/amd64 -t $(LINUX_IMAGE) -

LINUX_RUN = docker run --rm --platform linux/amd64 -v "$(abspath ..)":/src -w /src/port \
	-e SDL_VIDEODRIVER=dummy -e SDL_AUDIODRIVER=dummy $(LINUX_IMAGE) make BUILD=build/linux

linux: linux-image
	$(LINUX_RUN) all

linux-check: linux-image
	$(LINUX_RUN) all check smoke

# Windows x86_64 cross build with mingw-w64 and the official SDL2 mingw release.
# The compiler runs natively in the container; only its output is x86_64.
# Result: build/windows/alleycat.exe + SDL2.dll (copy both to the Windows box).
SDL_VERSION   := 2.32.10
WINDOWS_IMAGE := alleycat-windows-build

windows-image:
	printf 'FROM $(LINUX_BASE)\nRUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq make gcc-mingw-w64-x86-64 nasm python3 curl ca-certificates >/dev/null\nRUN curl -fsSL https://github.com/libsdl-org/SDL/releases/download/release-$(SDL_VERSION)/SDL2-devel-$(SDL_VERSION)-mingw.tar.gz | tar -xz -C /opt && mv /opt/SDL2-$(SDL_VERSION) /opt/SDL2\n' \
		| docker build -t $(WINDOWS_IMAGE) -

windows: windows-image
	docker run --rm -v "$(abspath ..)":/src -w /src/port $(WINDOWS_IMAGE) make TARGET_OS=Windows BUILD=build/windows all

# Web (WebAssembly) build with the official Emscripten image; the -arm64
# variant runs natively on Apple Silicon. Building the image compiles a
# program with the same flags once, so SDL2 and the system libraries are in
# its cache. Result: build/web/index.html, .js and .wasm; serve the
# directory over HTTP (e.g. python3 -m http.server -d build/web).
EMSDK_VERSION := 4.0.10
EMSDK_TAG     := $(EMSDK_VERSION)$(if $(filter arm64 aarch64,$(shell uname -m)),-arm64)
WEB_IMAGE     := alleycat-web-build

web-image:
	printf 'FROM emscripten/emsdk:$(EMSDK_TAG)\nRUN echo "int main(void) { return 0; }" > /tmp/t.c && emcc -O2 -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH -sENVIRONMENT=web /tmp/t.c -o /tmp/t.html && rm -f /tmp/t.*\n' \
		| docker build -t $(WEB_IMAGE) -

web: web-image
	docker run --rm -v "$(abspath ..)":/src -w /src/port $(WEB_IMAGE) make TARGET_OS=Web BUILD=build/web all

# Release archives in build/: the macOS build (always) plus the Linux and
# Windows builds if they exist (make linux / make windows first). Each holds
# one top-level directory named like the archive. Run on macOS.
PACK_NAME := alleycat-$(VER)
PACK_DIR  := $(BUILD)/pack

pack: $(TARGET)
	rm -rf $(PACK_DIR)
	mkdir -p $(PACK_DIR)/$(PACK_NAME)-macos-x86_64
	cp $(TARGET) $(PACK_DIR)/$(PACK_NAME)-macos-x86_64/
	cp -R $(SDL_DIR)/SDL2.framework $(PACK_DIR)/$(PACK_NAME)-macos-x86_64/
	rm -f $(BUILD)/$(PACK_NAME)-macos-x86_64.zip
	cd $(PACK_DIR) && zip -qry ../$(PACK_NAME)-macos-x86_64.zip $(PACK_NAME)-macos-x86_64
	@echo "pack: $(BUILD)/$(PACK_NAME)-macos-x86_64.zip"
	@if [ -f $(BUILD)/linux/alleycat ]; then \
		mkdir -p $(PACK_DIR)/$(PACK_NAME)-linux-x86_64 && \
		cp $(BUILD)/linux/alleycat $(PACK_DIR)/$(PACK_NAME)-linux-x86_64/ && \
		COPYFILE_DISABLE=1 tar -czf $(BUILD)/$(PACK_NAME)-linux-x86_64.tar.gz --uid 0 --gid 0 \
			-C $(PACK_DIR) $(PACK_NAME)-linux-x86_64 && \
		echo "pack: $(BUILD)/$(PACK_NAME)-linux-x86_64.tar.gz"; \
	fi
	@if [ -f $(BUILD)/windows/alleycat.exe ]; then \
		mkdir -p $(PACK_DIR)/$(PACK_NAME)-windows-x86_64 && \
		cp $(BUILD)/windows/alleycat.exe $(BUILD)/windows/SDL2.dll $(PACK_DIR)/$(PACK_NAME)-windows-x86_64/ && \
		rm -f $(BUILD)/$(PACK_NAME)-windows-x86_64.zip && \
		(cd $(PACK_DIR) && zip -qr ../$(PACK_NAME)-windows-x86_64.zip $(PACK_NAME)-windows-x86_64) && \
		echo "pack: $(BUILD)/$(PACK_NAME)-windows-x86_64.zip"; \
	fi
	rm -rf $(PACK_DIR)

$(BUILD):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

clean:
	@-rm -rf $(BUILD)/

.PHONY: all run check smoke clean linux-image linux linux-check windows-image windows web-image web pack

# Apple Silicon uses the portable C backend in a separate build directory.
arm64:
	$(MAKE) -f Makefile.arm64 all

arm64-check:
	$(MAKE) -f Makefile.arm64 check smoke gameplay import-check paths-check

arm64-app:
	$(MAKE) -f Makefile.arm64 app

.PHONY: arm64 arm64-check arm64-app
