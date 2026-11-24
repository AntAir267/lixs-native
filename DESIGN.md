# LEGO Island Xtreme Stunts native: design notes

LEGO Island Xtreme Stunts (Silicon Dreams / LEGO Interactive, PC, 2002): the game's program
recompiled to C for Linux, on SDL3 and Vulkan (DXVK's Direct3D 8 for now). This file is the
handoff record: decisions first, then game facts, then Progress. Shared rules:
the maintainer's working rules and goals for all ports.

## Decisions

### 1. Goal (2026-11-06)
"Lego Island Xtreme Stunts Vulkan/recomp": the standard job of the maintainer's other PC ports (an earlier C++/D3D8 port,
etm-native, hardtruck2-native): the user's own Game.exe translated to C, Windows and DirectX
replaced by native code, settings in the game's own menus, every controller working, enhanced
graphics as options once the game is fully functional (GAME-GOALS.md).

### 2. The game files (2026-11-06)
- Source: the maintainer's own "LEGO Island Xtreme Stunts (Europe) (En,De,Es,It) (Rerelease)" ISO (the 2005
  rerelease, no copy protection), unpacked locally.
- The disc holds the game's data at LIXS\CD (raw files, 400 MB: zones z00int ... z98psh, fmv
  MPEG-1 films, music .ogg + DirectMusic .sgt/.sty/.dls, comment speech) and LIXS\CDC (60 MB,
  the compressed set). The installer (Setup\<language>\Island Xtreme Stunts.msi, an admin image)
  copies only Game.exe, FindDisc.exe, game.dat and messages: the data is always read from the CD.
- Game.exe: 2002-11-20, MSVC 6, C runtime linked in, no relocations, title "Island Xtreme Stunts
  V0.43", sha256 f0429a43...; the English, German, Italian and Spanish installs ship the same file.
  Imports: d3d8 (D3DX8 linked in), DINPUT8, DSOUND (ordinals 1, 2, 11), WINMM, OLE32
  (DirectMusic 8 and DirectShow through CoCreateInstance), AVIFIL32/MSVFW32 (a capture tool),
  COMDLG32, SHELL32, ADVAPI32 (registry). Music in .ogg is decoded by its own linked-in Vorbis.
- Game folder ~/.local/share/lixs-native/game: links Game.exe, game.dat (the install's) and CD
  (the disc). Never written; the user folder ~/.local/share/lixs-native takes every write.

### 3. Translation (2026-11-06)
- tools/ and runtime/ forked from an earlier port of the same C++/Direct3D 8 line (1NSANE lineage via tdor3-native; the
  Direct3D 8 bridge to DXVK and our Vulkan renderer of Direct3D 8, DirectInput 8, DirectSound,
  DirectShow films, pad.c). That port's game-specific parts (its controller menus, rumble, the
  hand-written C in runtime/decomp) were left out; they are in work/ref for reference.
- builds/lixs.py: one module. Discovery: 5,952 functions, 433K instructions, 0 errors; 44 C files,
  1.33M lines.
- Ghidra cross-check (tools/ghidra/analyze.sh makes work/ghidra and work/ghidra/all/<addr>.c,
  every function decompiled; tools/ghidra_check.py compares at instruction level): of Ghidra's
  4,522 functions, 4,504 are ours (101 of them started inside our bodies and are EXTRA_ENTRIES,
  as the earlier port does), 18 are referenced by nothing (unused CRT maths dispatchers, empty stubs).
- Translator fixes made for this exe:
  - codegen.py: `div r/m8` wrote `%%` into the C (a format-string slip) [verified: compile error
    at 0x4026bf, the CPU check's Cyrix test].
  - codegen.py: popfd keeps EFLAGS.AC (bit 18) as well as ID; the game's CPU check (0x402640)
    toggles AC to tell a 386 and ID for CPUID [verified: disassembly]; without it the game
    thought it had no CPUID and its per-processor table stayed empty (crash at 0x402b30).
  - discover.py: no function starts on padding (0x90 / 0xcc). A data word pointing at the nops
    before 0x425550 made the padding a function, and the real vtable method 0x425550 was then
    refused as inside it [verified: run stopped at a call through vtable 0x56be20].
  - recomp.py: NORETURN_ADDRS (noreturn functions by address, for a linked-in CRT),
    INT3_CONTINUE (int3s decoded as nops), gen/insns.txt (each function's instructions).
  - Three vtable methods that other functions tail-jump to were absorbed into those before their
    pointer was seen: 0x42c740, 0x42c760 (vtables 0x56ba98, 0x56cbf8, 0x56cca4), and 0x444e50
    (a table at 0x5ad128): EXTRA_ENTRIES. [guess] a few more such may turn up as fatal "call to
    ... code the translator did not find as a function"; gen/rejected.txt lists 111 candidates
    refused as inside other functions, most of them not code.
- The CRT's fast memcpy/strlen hooks (crt.c) and the C++ continuation hook (eh.c) are weak until
  builds/lixs.py names the linked-in CRT's addresses (not done; the translated CRT runs).

### 4. Start-up (2026-11-06)
- AutoRun.exe starts `Game.EXE "D:\LIXS\CD\" "D:\LIXS\CDC\"` [verified: AutoRun.exe strings].
  Game.exe's start (0x4014a0) needs both: argv[1] opened as "SDDFS Raw", argv[2] as "SDDFS
  Compressed" (0x4091a4), and quits without them. game.c gives that command line; the program
  sees itself as C:\LIXS\Game.exe and the disc as D:.
- 0x401640 reads Game.dat beside the exe (argv[0] with its extension replaced): 192 bytes whose
  first 16 dwords must equal the table at 0x56b3e0, else it quits [verified: decompile].
- Registry HKLM\Software\LEGO Interactive\Island Xtreme Stunts: Location and Language are read
  (0x401730); missing is fine so far (the key is created).
- Asserts: 0x4446f0 (file, line, expression) prints through OutputDebugStringA and executes int3,
  which on Windows ends the game; inlined asserts call it and then `test al, al / je / int3`.
  game.c hooks it: logged (LOG_ALWAYS "assert: ...") and returns AL = 0; INT3_CONTINUE turns its
  own int3 into a nop so the translator sees it return. LIXS_ASSERT_FATAL=1 stops as the original.

### 5. Music: DirectMusic 8 stand-in (2026-11-06)
- runtime/dmusic.c (forked from li2-native's DirectX 6/7 one): Performance8, Segment8,
  SegmentState8, AudioPath8, IDirectMusic8, and for every loaded object IDirectMusicObject +
  IPersistStream. The music is GothicKit dmusic (third_party/dmusic, li2-native's patched copy)
  rendered as a mixer source. Segments are loaded from the bytes the game's own IStream gives
  Load (named "mem:<n>" for the engine); their references are resolved by file name from the
  disc's music folder.
- What the game needs is in work/dmusic-inventory.md (a full read of its DirectMusic use, with
  addresses). The facts that shape the stand-in [verified there]: every ambience / building
  segment plays as a SECONDARY segment on its own audio path (16 standard stereo paths made at
  start, 3D paths with an IDirectSound3DBuffer for positioned sounds; volume per path); the game
  polls GetNotificationPMsg every 20 ms and must get SEGEND for one-shot segments it still holds,
  with punkUser QI'ing to the very SegmentState8 pointer it holds (else it dereferences NULL);
  PlaySegmentEx must always give a state; SetParam(GUID_CommandParam) without a command track ->
  DMUS_E_TRACK_NOT_FOUND; the game's loader caches an object only if GetDescriptor reports
  DMUS_OBJ_OBJECT (else the 4.3 MB DLS is reloaded 22-45 times per file); 36 segments, 5 styles,
  7 DLS collections live in the zones' Sounds folders.
- Built (same day): one voice per segment state, each with its own GothicKit performance, mixed
  at its audio path's volume (3D paths: inverse distance from the min distance and pan from x,
  from IDirectSound3DBuffer::SetAllParameters); a one-shot voice ends after the segment's
  length on the game's clock and GetNotificationPMsg then gives SEGEND with punkUser = the
  state object the game holds; loops play again at their end; pFrom and primaries replace
  silently; GetDescriptor reports the file's own GUID (top-level guid/dlid chunk) so the loader
  caches; a segment's folder is found by matching its bytes against the disc's .sgt files and
  each folder has its own engine loader (three different "DLS Collection1.dls" exist).
  Groove levels (SetParam GUID_CommandParam) are not passed to the engine: answered
  DMUS_E_TRACK_NOT_FOUND, so the game skips its groove replays [to do].
- Checked: z01lgi's segments load (Amb_City ... 48 s each, Bui_partyhouse 16 s) from their own
  folder. Not yet checked: playback and SEGEND (no ambience was started in the scripted runs
  up to free roam); listen in a real-time run with an output.

### 6. Windows behaviour fixed for this game (2026-11-06)
- ReadFile honours OVERLAPPED (offset, Internal/InternalHigh, its event set): the .ogg streaming
  reads that way and waits on the event (0x430900).
- MultiByteToWideChar / WideCharToMultiByte fill what fits before failing as Windows does: the
  game converts with the length minus the terminator and adds the terminator itself (0x42f6b0).
- --test scheduler: an empty PeekMessage yields (the loading thread 0x4010a0 spins on it); a
  timed-out wait advances the virtual clock by its timeout (max 50 ms) when the main thread is
  blocked untimed (the main thread waits for a stream's read while the sound thread ticks).
- These fixes in inherited code went to the maintainer's shared fixes list and into AntiStatic.

### 7. On AntiStatic (2026-11-12/13; merged to main 2026-11-13)
The AntiStatic toolkit's maintainers (2026-11-12: every relevant game fully onto the toolkit) asked
LIXS to move onto it for everything but graphics (AntiStatic's docs/MOVING.md).
- third_party/antistatic: the toolkit as a submodule, pinned at v0.2.2 since 2026-11-13 (its translator, CPU core,
  Windows layer, DirectInput, pad, DirectSound, DirectMusic 8 with its dmusic engine). The port
  keeps runtime/port.h (CD label now the disc's real LIXSRGN2: the game never asks),
  runtime/port_lixs.c (dialog, CRT hooks, DirectMusic class, film idle), d3d8.c (fixed_modes),
  DXVK, vk_*, video.c, crt.c, game.c. Moving to a newer toolkit: move the pin, run the route.
- Link order (fixed at the source in v0.2.2: imports are weak references with separately named
  missing_<sym> stand-ins; the runtime-first order stays, harmless): the runtime links before the generated code. The translator writes a weak
  "missing import" stub for every import and the toolkit's defaults (API_WEAK) are weak too, so
  the first one the linker meets wins; with the generated code first, GetSystemInfo and every
  winmore.c call fell to the stub (a FATAL at start-up) [verified: addr2line of the symbols].
- Baselines (main's build before the move): tools/route1.sh (menus, new game, intro, the first
  stunt, free roam; 4200 frames, a memory hash every 50, frame dumps at 880/2300/3500/4190), run
  twice: identical. Kept in work/base/a (binary work/base/lixs-own, translation work/base/gen-own).
  tools/route_cmp.py compares a run with them.
- Translation: the toolkit's gives 5,994 functions vs 5,952; every function and instruction of
  ours is in it, plus 42 entries and 125 instructions (an SEH handler and functions reached only
  by jumps, ANTISTATIC.md's lixs notes); 0 warnings (ours had 8 SSE ones).
- v0.2.0 froze in --test before frame 1 (it lacked LIXS's two scheduler fixes); reported, and in
  v0.2.1. On v0.2.1 the route runs clean to frame 4200 (no asserts, crashes or missing calls).
  Memory hashes differ from the first point (frame 50) with ~4,300 fewer runtime calls by then:
  a different runtime (handles, atoms and call counts differ, as the toolkit's first move found), so
  they can't be compared across the move; the next baselines are taken on this build. Frame
  3500 is identical to the old build's, 2300 one pixel off, 880 and 4190 differ only in the
  sparkle particles (timing) [verified: difference masks, work/base/f880cmp.png].
- v0.2.2 (regenerated): the route matches v0.2.1's run exactly (84 hashes, call counts, 4 frames)
  [verified: tools/route_cmp.py work/base/v021 work/base/at5].
- v0.2.38 (2026-11-18, the first public toolkit tag; regenerated): the route runs clean; frames
  and runtime call counts identical to v0.2.2's at all 84 points; memory hashes differ from frame
  50 on [guess: toolkit changes to state inside the runtime between v0.2.2 and v0.2.38; not traced].
  work/base/at038 is the reference from then on.
- v0.2.46 (2026-11-23; regenerated): portable x87 maths and SSE reciprocals on by default (the
  same results on every PC). The route matches v0.2.38's run exactly on this machine (84 hashes,
  call counts, 4 frames) [verified: tools/route_cmp.py work/base/pub1 work/base/at046], although
  the game has 108 sin/cos/atan/log/2^x sites; work/base/at046 is the reference from then on.
  Generated objects now depend on port.h and the toolkit's cpu headers (cpu.h reads port.h).
- The Vulkan renderer this port inherited (vk_*.c, shaders/, its notes) was removed 2026-11-18: LIXS
  never used it (DXVK is its only path; option renderer = vulkan was never tried) and the toolkit
  will bring its own. The route after the removal matches v0.2.38's run exactly (84 hashes, calls,
  frames). Checks on real hardware (a handheld, a 2 GB GCN 1 Radeon) are in the maintainer's notes.

### 8. Public copy (2026-11-18)
- The private repository stays the true copy; the public
  repository AntAir267/lixs-native gets fresh exports with no history (the history has a private
  address and names), made by tools/export_public.sh TAG: HEAD's tree minus CLAUDE.md, PRIVATE.md,
  tools/deck_add_to_steam.py and the private submodule link; the toolkit as the public AntiStatic
  repository at TAG; then the toolkit's tools/audit_public.py and this port's own list of private
  references must pass. Commit there with the GitHub noreply address and push.
- Done for it: README, CONTRIBUTING, CREDITS, LICENSE (Unlicense), issue templates, .recomp.json
  ("toolchain": "AntiStatic"); the inherited Vulkan renderer removed (decision 7); the DXVK patches
  and build steps are the toolkit's (third_party/README.md); notes about the maintainer's own
  machines moved to PRIVATE.md.

### 9. Controller (2026-11-23)
Asked for: controller support like the LEGO Island 2 port's. As LI2: the
game's own DirectInput joystick support is used, the toolkit's pad.c merges every controller into
the one joystick the game sees (PORT_PAD_JOYS 1: there from start-up in real runs and in --test
runs with LIXS_PAD, so a controller plugged in late feeds it and no re-enumeration is needed), and
the port's port_pad_send (runtime/port_lixs.c) sends it in the layout the game expects.
- How the game reads input [verified: disassembly / Ghidra decompiles]: an input manager
  (0x418340, at 0x5eb694; devices 0 keyboard, 1 mouse, 2.. joysticks of DI8DEVTYPE_JOYSTICK or
  _GAMEPAD, 0x4187f0; ours is 0x15) gives each device a 32-entry action table; 0x418580 reads a
  device as a word of actions (bit n = action n) plus an analogue X/Y. Joystick codes: 0-3 X/Y
  past 200 of the -1000..1000 range it sets (0x4264f0; analogue beyond, 0x426180), 4.. button
  n-4; POV and other axes unread. Each frame the main loop (0x447340) gives device 0 and device 2
  either the player's tables (0x6a08a0 / 0x6a09a0: Options > Controls, which rebinds actions 4, 5,
  6, 7, 17, 10, 12, 13, 0x514250) or fixed default ones for the menus (0x6a0920 / 0x6a0a20), reads
  both and ORs the joystick's word into the keyboard's. Menus: actions 0-3 move, 6 (Return)
  confirms, 16 (Escape) goes back (pressed word 0x60de80; 0x5066e7, 0x508097, 0x50f44c).
- Defaults (0x5147c0; keyboard table 0x578288 = DIK + 0x20; the joystick's through 0x426490,
  whose known-pad name list 0x426630 our name doesn't match, so the 10-button table 0x56be3c):
  action 4 Right Ctrl / button 1 (pizza, brake), 5 Right Shift / 2 (jump, accelerate), 6 Return /
  3 (talk, interact, skateboard, leave, land, stunt held with the arrows, power-up), 7 Space / 4
  (play, horn), 17 Backspace / 5 (PDA), 10 "/" / 6 (camera, held with the arrows), 12 "," / 7
  (zoom), 13 "." / 8 (camera reset, held first person), 16 Escape / 9 (pause). Meanings from the
  manual's control tables (disc Bedienungsanleitung/IslandX_G.pdf).
- Xbox layout: A or RT jump / accelerate; B talk / action / stunt; X or LT pizza / brake; Y play /
  horn; Back PDA; LB zoom; RB camera reset; Start pause; left stick or D-pad (full lock while the
  stick rests) walk / steer / menus; right stick the camera (past 40 % it holds button 6 and is X/Y
  instead of the left stick, the manual's "/" + arrows); right stick click button 6, left stick
  click button 10 (free for rebinding); A, B or Start skip a film. In menus (the joystick given the
  fixed table: hooks on 0x409e30 / 0x409df0, game.c) A is Return and B Escape, as on a console.
  The keyboard is unchanged and works alongside.
- Checked (2026-11-23, on a Radeon 890M handheld): tools/route1.sh (keyboard) after the change is
  identical to work/base/at046 (84 hashes, call counts, 4 frames): without LIXS_PAD a --test run
  has no joystick and the hooks change nothing. The same route driven by LIXS_PAD alone (a
  controller plugged in at frame 50, Y for Space, A for Return, a second controller holding the
  stick up for the stunt) reaches the same places: frames 880, 3500, 4190 identical to the
  keyboard run's, 2300 differs in one small spot (sparkles). The log shows the menu layout from
  frame 3 to 1477 and 1927-2101, the play layout otherwise.
- [guess] the clapperboard name entry and the screens after the stunt use the play table, so a
  player confirms them with B (Return) there; the route's A presses at 1500/1800/3400 (play layout)
  still matched, apparently because those screens take any action key [guess: not traced].
- The camera is [unverified]: in both pad runs a clapperboard overlay came up in free roam from
  about frame 4280 (with or without the right stick: a game event, not traced) and the scenery did
  not turn while RX was held (4250-4370), neither with the first try (the right stick moving the
  mouse, LI2's way; dropped) nor with "/" + X/Y. To check by hand, or with a route that pushes the
  right stick before 4250. Pad run 2 (work/c2/pad) matched run 1's frames to 4240 exactly.
- Not done: rumble (the game asks for none we know of); prompts showing pad buttons; whether a
  saved Controls page binding made with no joystick present carries bad joystick codes [guess].

## Progress

### 2026-11-06: first runs
- Builds; a --test run gets through CPU detection, creates the Direct3D 8 device on DXVK (640x480
  R5G6B5 windowed, D24S8), loads textures from the CD, and stops at music: the game creates
  DirectMusic 8's Performance (CLSID d2ac2881, IID_IDirectMusicPerformance8), asserts
  REGDB_E_CLASSNOTREG (MSMusic.cpp line 81) and then uses the null object (crash in 0x497270's
  callees).
- Then (same day): the title screen. A --test run of 900 frames ends without an assert, crash or
  missing call; frame 880 shows the title over the 3D intro scene (work/run1/f880.png).
- Was next: a DirectMusic 8 stand-in (decision 5).
- Scripted --test run (LIXS_KEYS="900:SPACE,1200:RETURN,1500:RETURN,1800:RETURN,2100:SPACE,
  2400:UP*600,3100:SPACE,3400:RETURN", 4200 frames): main menu, new game, the clapperboard name
  entry, the intro cutscene with subtitles, the first stunt (driving off the ramp), the cutscene
  after it, a loading screen, free roam on the island at frame 4190. No asserts, crashes or
  missing calls.

- A real-time windowed run (2026-11-06): renders, music plays, 30 s without a crash. Two fixes it
  needed:
  - The display modes: real runs listed only the desktop's (inherited); the game asks 640x480
    R5G6B5, took 3840x2160 instead and rebuilt its device from its loading thread while the main
    thread still used the old one (crash in CreateTexture). Real runs now list the fixed 16- and
    32-bit modes as --test runs do (d3d8.c fixed_modes).
  - The COM object table was realloc'd under a lock but read without one (now the toolkit's).
  - Uncapped (vsync off) it runs at 500-800 fps; the game may have high-frame-rate bugs as LEGO
    Island 2 did: to check.
  - Full screen (the default) stretches the 640x480 picture to the screen: 4:3 stretched until
    the resolution / widescreen work.

### Next
1. A play report (frame rate when focused, controls, sound); hear the ambiences' ends.
2. Resolution and widescreen (the game runs 640x480 R5G6B5 windowed; its own mode list).
3. Controller: done (decision 9); rebinding is the game's own Controls page.
4. Settings in the game's own Options menu; then the Outro film (DirectShow MPEG-1, video.c).
5. Ghidra cross-check is clean; lockstep (tools/lockstep.py, ref/) not yet run for this game. What the game needs [verified: decompiles of
  0x42f2d0, 0x433080, 0x433150, 0x4332d0]: IDirectMusicPerformance8 created through COM; the game
  implements its own loader (it reads the SDDFS packs) and makes each segment, style, band, DLS
  collection with CoCreateInstance(class, IID_IDirectMusicObject d2ac28b5), then QueryInterface
  IPersistStream and Load(its own IStream). So the stand-in must load those objects from an
  IStream (GothicKit dmusic, as li2-native's dmusic.c, reads from memory). The music .ogg files
  are the game's own Vorbis decoder over DirectSound.
- Also seen (DirectShow, for the .mpg films): CLSIDs e436ebb3 (FilterGraph), 336475d0, 79376820,
  1e651cc0; runtime/video.c plays MPEG-1 through FFmpeg.

## Tools
- `make` (PY = a Python with Capstone; the crazycity-insanity venv is found by default) builds
  build/lixs; JOBS=6.
- `build/lixs --test --user work/run1 --seconds 30 --log api,file,reg,d3d,thread`: a headless,
  deterministic run; the log is in the user folder and stdout.
- tools/ghidra/analyze.sh (once), tools/ghidra/decomp.sh <addrs>, tools/ghidra_check.py.
- work/game.asm: objdump of .text (made by hand: `objdump -d -M intel --start-address=0x401000
  --stop-address=0x56a000 Game.exe`).
