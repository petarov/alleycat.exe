# Building Alley Cat (port)

## Native Apple Silicon (ARM64)

A C-backend build is available for macOS ARM64. It does not require NASM, Rosetta, Ghidra or a pre-existing Intel executable. The original build targets below remain available.

Requirements: Xcode command line tools, Python 3, and the official universal SDL2 2.32.x framework in `port/third_party/SDL2.framework`. Supply your own original DOS `CAT.EXE` in the repository root (55,067 bytes; SHA-256 `4979c8867826e08881d1d4bec95c95d6bc1ac70fd21050ccdb4f733a50d7bfdd`). Other versions fail validation before conversion.

From the repository root:

```sh
python3 -m venv port/build/arm64-venv
port/build/arm64-venv/bin/pip install -r port/requirements-arm64.txt
cd port
make -f Makefile.arm64 PYTHON=build/arm64-venv/bin/python3 all check smoke gameplay import-check paths-check app
```

Outputs are in `port/build/macos-arm64-external/`: `alleycat`, headless test programs and **Alley Cat.app**. The ARM64 app includes SDL2 and the translated game logic, but leaves the original data segment zero-initialized. It asks for the supported original `CAT.EXE` on first launch, checks its SHA-256, imports it into the current user’s Application Support folder, and loads its data segment before starting the game. No Homebrew libraries are needed on the target Mac. The app bundle stays unchanged after import. Validated copies in the former `RetroShopPT/Alley Cat/` folder are migrated automatically to `Alley Cat/`, keeping the original copy. If migration cannot be saved, the app can still use the validated legacy copy without a new import. Its signature is ad-hoc, not Apple notarization. The optional packager `--icon` argument accepts a locally supplied `.icns`; no proprietary game artwork is added to source control.

`MACOS_MIN` defaults to `11.0`; use `MACOS_MIN=27.0` if building specifically for macOS 27. Native execution and packaging were tested on M1 Pro/macOS 27.2. Older macOS versions have not been physically tested. `make arm64`, `make arm64-check` and `make arm64-app` are convenience entry points in the original Makefile; set `PYTHON` to the same virtual-environment interpreter.

The build uses `tools/dos2listing.py` with Capstone 5.0.9 to follow reachable 8086 instructions, explicitly seed the interrupt/helper entries and read the eight room-dispatch targets. It applies the DOS relocation table at load segment 1000h before decoding. The existing `asm2c.py --external-data` then translates the listing into C without embedding the original data segment; hardware, flags, timing, input and sound use the upstream portable runtime. The supported executable produces 8,331 translated instructions and zero unsupported-instruction traps. The generated listing and C include original game material and must remain outside source control.

Tests:

- `check`: runs the generated title drawing and six blits with a fixed random seed, then compares all 16,384 framebuffer bytes with `tools/ref_title.py`.
- `smoke`: runs the game without a window for 10 seconds (override `SMOKE_SECONDS`).
- `gameplay`: sends keyboard scancodes through the title, Kitten selection and game start, then verifies cursor input changes the cat position while the game is alive.
- `import-check`: accepts the correct original, rejects missing/truncated/corrupt files and confirms rejected imports leave game memory unchanged. The headless tests explicitly load the build-time CAT.EXE through a test-only object that is never linked into the app.

`paths-check` verifies the new import folder, migration from the legacy folder, preservation of the original, current-folder preference, corrupt-copy rejection and fallback when migration cannot be saved.

For UI checks, `ALLEYCAT_DATA_DIR` can select a temporary import folder. Normal launches use `~/Library/Application Support/Alley Cat/`. The original DOS file is still required at build time to generate the translated game logic. The optional `--external-data` generator argument leaves legacy x86-64/web builds unchanged; the ARM64 build always enables it.

These are focused validation checks, not a complete playthrough or a sound-fidelity certification. This path leaves x86-64, Linux, Windows and web targets unchanged.

## Requirements

Original x86-64 and web build paths (the ARM64 path above uses a runtime data import):

- The game was written in hand-coded 8088 assembly. This project disassembles `CAT.EXE`, translates the 16-bit code into x86-64 assembly, and runs it on emulated PC hardware with SDL2 handling video and sound.
- The original `CAT.EXE`, in the repository root. The build reads the game data directly from it, and every binary embeds it, so no game files are needed at run time. `CAT.EXE` and the disassembly derived from it are copyrighted by their original owners.
- `re/CAT.asm`, the Ghidra listing (see [Regenerating the listing](#regenerating-the-listing)).

macOS (native build, x86_64, runs under Rosetta 2 on Apple Silicon):

- Xcode command line tools (clang, make, zip)
- nasm (`brew install nasm`)
- Python 3. It is used at build time to generate the translated code.
- SDL2, as the official macOS release framework (2.32.x, universal). Copy `SDL2.framework` to `port/third_party/SDL2.framework`. If macOS refuses to load it ("library load disallowed by system policy"), remove the download quarantine: `xattr -dr com.apple.quarantine port/third_party/SDL2.framework`.
- Docker Desktop, only for the Linux and Windows builds from macOS (`make linux`, `make windows`).

Linux (native build, x86_64 host):

- make, gcc, nasm, python3, pkg-config and the SDL2 development package. On Debian / Ubuntu / Linux Mint: `sudo apt install make gcc nasm python3 pkg-config libsdl2-dev`.

## Building and running

From `port/`:

- `make` builds `build/alleycat` for the host (macOS or Linux)
- `make run` builds and starts the game
- `make check` is a headless test. It runs the translated title backdrop code with a fixed random seed and compares the resulting screen memory with an independent Python model (`tools/ref_title.py`).
- `make smoke` is a headless test. It runs the game without a window for a few seconds and writes the screen memory to `build/vram_smoke.bin`. Set the duration with `SMOKE_SECONDS=20`. Convert the dump to an image with `tools/vram2ppm.py`.
- `make clean` removes `build/`. Run it after changing the SDL version: the objects do not depend on the SDL headers.

Messages about unsupported BIOS calls or I/O ports are printed once each to the terminal (not on Windows, which has no console window).

The version shown on the title screen is `VER` in `port/Makefile`.

### Linux and Windows builds from macOS

Both run in Docker. Each target builds its toolchain image once (a few minutes) and reuses it afterwards.

- `make linux` builds `build/linux/alleycat` in an emulated x86_64 Debian 13 container. The binary needs glibc 2.34 or newer and SDL2 (`libsdl2-2.0-0`) on the target system, e.g. Debian 12+, Ubuntu 22.04+, Linux Mint 21+.
- `make linux-check` also runs `check` and `smoke` in that container.
- `make windows` cross-compiles `build/windows/alleycat.exe` with mingw-w64 and copies `SDL2.dll` next to it. The image downloads the official SDL2 mingw release (`SDL_VERSION` in the Makefile). Copy both files to the Windows machine.

### Release archives

`make pack` (macOS) builds the macOS binary and writes to `build/`:

- `alleycat-<VER>-macos-x86_64.zip`: `alleycat` and `SDL2.framework`
- `alleycat-<VER>-linux-x86_64.tar.gz`: `alleycat`, if `build/linux/alleycat` exists
- `alleycat-<VER>-windows-x86_64.zip`: `alleycat.exe` and `SDL2.dll`, if `build/windows/alleycat.exe` exists

`pack` does not rebuild the Linux and Windows binaries; run `make linux` and `make windows` first. The macOS binary is not signed or notarized, so a downloaded copy needs `xattr -dr com.apple.quarantine <folder>` or approval under Privacy & Security.

## Layout

- `re/` holds the reverse-engineering material.
  - `re/CAT.asm` is the Ghidra listing of the code segment. `<NOT INCLUDED>`
  - `re/DumpListing.java` is the Ghidra script that produces it.
  - `re/NOTES.md` covers the memory layout, subsystems, key table, hotkeys and open questions.
- `port/tools/asm2nasm.py` translates `re/CAT.asm` into `port/build/cat_code.asm`. Every original instruction gets a label `L_xxxx` after its code-segment offset. Its `MARKERS` table holds the port's few changes to the game code (skipped joystick question, Ctrl-L, the extra help line), and `CPU_PACE_NS` slows the two software-driven sound loops to the original speed.
- `port/src/runtime.inc` holds the execution model: how 8086 registers, segments, string instructions, flags and BIOS/port calls map to x86-64, and the calling-convention differences between macOS/Linux and Windows. The generated code and the hand-written code both use it.
- `port/src/game.asm` holds the hand-written parts: the entry and exit, the segment lookup, and the code ranges the generator skips (interrupt vector setup and the exit path).
- `port/src/data.asm` includes the original data segment straight from `CAT.EXE`, so data offsets match the listing. All game text, including the menu strings, comes from there. Text added by the port is stored past the end of the original data.
- `port/src/bios.c` emulates the hardware: the BIOS services (INT 10h, 11h, 1Ah), the timer chip, the CGA registers, the speaker ports, the keyboard and the memory segments.
- `port/src/font8x8.c` is the 8x8 font for BIOS text output, drawn for this port in CGA style.
- `port/src/speaker.c` emulates the PC speaker on an SDL2 audio device.
- `port/src/platform.c` is the SDL2 front end: the window, display of the CGA screen (with the version overlay on the title screen), keyboard events and the game thread.
- `port/tests/` holds the headless test programs used by `make check` and `make smoke`.

## How it works

The original program is translated, not emulated instruction by instruction. The 8086 registers keep their 16-bit names in the generated code. DS and ES become base pointers into emulated 64 KB segments. Interrupts and port accesses become calls into C.

The game runs on its own thread, busy-waiting on the timer as it did on the original PC. The main thread plays the part of the display hardware: it converts the emulated CGA memory to pixels at 60 Hz, handles window events, and writes key presses into the game's key table, as the original keyboard interrupt handler did. Speaker changes are timestamped on the game thread and replayed on SDL's audio thread, so the sound keeps the game's timing.

## Regenerating the listing

`re/CAT.asm` was produced with Ghidra 12.1.4. To regenerate it, run this from the repository root:

```
mkdir -p /tmp/ghidra-proj
analyzeHeadless /tmp/ghidra-proj cat -import CAT.EXE -scriptPath re \
    -postScript DumpListing.java re/CAT.asm re/stats.txt -deleteProject
```

The script corrects the operand order of `MOV Sreg,r/m` instructions, which Ghidra prints reversed. It also marks the keyboard interrupt handlers and the room dispatch targets as code, because Ghidra does not find them on its own.
