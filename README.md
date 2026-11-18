# LEGO Island Xtreme Stunts, native

LEGO Island Xtreme Stunts (Silicon Dreams / LEGO Software, 2002, PC) running natively on Linux, on SDL3 and
Vulkan.

The game's own `Game.exe` is translated into C on your machine and built against a stand-in for the parts of
Windows it uses: Direct3D 8 (drawn with Vulkan through DXVK), DirectInput, DirectSound, DirectMusic and
DirectShow. **This repository holds no part of the game.** You need your own copy (the disc), and the build makes
its translation from it.

## Where it stands

- Early. Scripted test runs play the menus, a new game, the intro, the first stunt and free roam on the island
  with no crashes; it runs full screen with sound on Linux and the Steam Deck. It hasn't been played through,
  so expect rough edges.
- The game's own logic runs unchanged: the translated program is the original's code, instruction for
  instruction.
- Keyboard only for now; controller support, widescreen and settings in the game's own menus are next.
- Linux only for now.

## Building

You need: gcc, make, cmake (for SDL3), meson and ninja (for DXVK), Python 3 with `capstone` (5.x), the Vulkan
headers and loader, zlib, and FFmpeg's development libraries (libavformat, libavcodec, libswscale,
libswresample, libavutil).

0. Clone with the toolkit: `git clone --recursive`, or `git submodule update --init` in a clone you already
   have. The translator and the Windows stand-in come from
   [AntiStatic](https://github.com/AntAir267/AntiStatic-Recomp-Toolkit), pinned in `third_party/antistatic`.
1. Build SDL3 and DXVK into `third_party/` (the commands are in `third_party/README.md`).
2. Make the game folder. The build is made for the 2005 rerelease disc (Europe, En/De/Es/It), whose
   `Game.exe` (3,235,840 bytes, sha256 `f0429a436838c1b28ee5310958eade2ef90f5af6bb19889e3ea86e5c74217f0f`) is
   the same in all four languages; other releases may need changes in `builds/`. Put in
   `~/.local/share/lixs-native/game`:
   - `Game.exe` and `game.dat` from the disc's `Setup/<language>/program files/LEGO Interactive/Island Xtreme Stunts/`
   - `CD`: the whole disc (a copy of its files, or a link to where it's mounted)
3. Translate and build:

       python3 -m venv .venv && .venv/bin/pip install capstone==5.0.7
       make

   The first build translates the whole program (a minute or two) and compiles it (a few minutes); later
   builds only recompile what changed. `make GAME=/other/folder` uses another game folder.
4. Run `build/lixs`, or `make install` to put it in `~/.local/opt/lixs-native` with an applications-menu
   entry. Its settings and saves live in `~/.local/share/lixs-native`. With no arguments it starts full screen
   (`display = window` in `options.txt` for a window).

## Checks

`tools/route1.sh build/lixs OUT` plays a scripted route (menus, new game, intro, the first stunt, free roam:
4,200 frames) on a virtual clock, writing a memory hash every 50 frames and four frame dumps;
`tools/route_cmp.py A B` compares two such runs. A change that shouldn't alter the game must leave the route's
hashes, call counts and frames as they were. `tools/ghidra_check.py` compares the translator's functions with
Ghidra's.

## Licence and credits

The Unlicense: public domain (see `LICENSE`). LEGO Island Xtreme Stunts belongs to its owners, and nothing of it
is here. See `CREDITS.md` for where the code came from, and `DESIGN.md` for how it all works, with the details
of every fix. Contributions are welcome: please read `CONTRIBUTING.md` first.
