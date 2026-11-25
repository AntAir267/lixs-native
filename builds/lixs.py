"""LEGO Island Xtreme Stunts (Silicon Dreams / LEGO Interactive, 2002, PC): the files the user's
installation must hold and what the translator needs to know about them (addresses only; no code
or data from the game lives in this repository; DESIGN.md decision 2)."""

NAME = 'LEGO Island Xtreme Stunts (Game.exe of 2002-11-20, V0.43)'

# One module: Game.exe, MSVC 6 with the C runtime linked in, no relocations. The English, German,
# Italian and Spanish installs of the 2005 rerelease disc ship the same file.
MODULES = [
    dict(file='Game.exe', base=0x00400000, load='start',
         sha256='f0429a436838c1b28ee5310958eade2ef90f5af6bb19889e3ea86e5c74217f0f'),
]

# Host imports that never return
NORETURN_IMPORTS = ['ExitProcess', 'ExitThread', 'FatalAppExitA', 'FatalExit', 'TerminateProcess']

# The linked-in C runtime's functions that never return (by address; filled from the Ghidra
# cross-check)
NORETURN_ADDRS = []

# Function starts Ghidra 12.1.4 finds inside bodies discovery made (cross-check 2026-11-06,
# tools/ghidra_check.py: every other Ghidra function is ours, except 18 nothing references: unused
# CRT math dispatchers, empty stubs); split where Ghidra starts a function
EXTRA_ENTRIES = [
    0x00401840, 0x00401860, 0x00401b10, 0x004022f0, 0x00406180, 0x004067a0, 0x00418550, 0x0041a1d0,
    0x0041d680, 0x0041f9f0, 0x00420f40, 0x00430650, 0x00430690, 0x004306d0, 0x00431fa0, 0x00432640,
    0x0043d940, 0x0043dea0, 0x0043e310, 0x00444f50, 0x0044ac60, 0x0044d0b0, 0x0044d100, 0x0047afa0,
    0x0047e970, 0x00483670, 0x004837b0, 0x0048e080, 0x00494020, 0x004944e0, 0x004a2b10, 0x004a30f0,
    0x004a66a0, 0x004a7730, 0x004a78b0, 0x004a7f00, 0x004a9110, 0x004a9200, 0x004a9540, 0x004a97c0,
    0x004a9b20, 0x004a9ed0, 0x004aa3b0, 0x004aa4b0, 0x004aa570, 0x004aa680, 0x004aa880, 0x004aa8a0,
    0x004aa8d0, 0x004aa8f0, 0x004b3480, 0x004b3d30, 0x004bd2e0, 0x004bef50, 0x004bf5a0, 0x004c2790,
    0x004c6200, 0x004c7440, 0x004c7c20, 0x004c80e0, 0x004cf5e0, 0x004d1300, 0x004de390, 0x004de3d0,
    0x004de8d0, 0x004e7d40, 0x004e9e70, 0x004edf30, 0x004f4110, 0x004f4950, 0x004f4e10, 0x004f5e40,
    0x004fa150, 0x004fa990, 0x004fad70, 0x005184c0, 0x00519db0, 0x0052b790, 0x0052bb30, 0x0052c0c0,
    0x0052e0a9, 0x005308a4, 0x00533549, 0x00535afe, 0x00535b0b, 0x00535b49, 0x00538020, 0x0053a841,
    0x0053c946, 0x00544a10, 0x00547bf0, 0x00548be0, 0x00568610, 0x00568ef0, 0x00569080, 0x005690a0,
    0x005690e0, 0x00569180, 0x00569580, 0x005695a0, 0x00569710,
    # methods in vtables (0x56ba98, 0x56cbf8, 0x56cca4) that functions tail-jump to, absorbed into
    # those (0x42d2f0, 0x42d300) before the vtable pointer was seen; a callback in a table at 0x5ad128
    0x0042c740, 0x0042c760, 0x00444e50,
]
NOT_ENTRIES = []
JUMP_TABLES = {}
NONLOCAL_JUMPS = []

# Functions the runtime replaces or wraps (hook_<name>; returns 1 if it did the call itself)
HOOKS = {
    # the linked-in C runtime's hand-written memory and string routines (runtime/crt.c) [verified:
    # MSVC 6's byte patterns: memcpy and memmove share code (overlap allowed); strcpy 0x52e510
    # jumps into strcat's copy loop; Ghidra's FID names strlen, memset, strncpy]
    0x0052fb80: 'crt_memcpy', 0x005354f0: 'crt_memmove', 0x0052fb20: 'crt_memset',
    0x0052e400: 'crt_strlen', 0x0052e510: 'crt_strcpy', 0x0052e520: 'crt_strcat',
    0x0052d820: 'crt_strncpy',
    # the game's assert (cdecl: file, line, expression): logs through OutputDebugStringA then
    # int3, which on Windows ends the game; logged and carried on from (game.c)
    0x004446f0: 'game_assert',
    # the input manager's wrappers (cdecl): 0x409e30 (device, table) gives a device its 32-entry
    # action table, 0x409df0 (device, out) reads a device's actions as a bit word; the controller's
    # menu layout (game.c) [verified: disassembly 0x409df0-0x409e45, callers 0x4473d0-0x447414]
    0x00409e30: 'input_bind', 0x00409df0: 'input_read',
    # widescreen (runtime/widescreen.c): the projection builder (cdecl: camera + 0x9c, fov, aspect,
    # near, far, centre x, y) and the view corners for culling (cdecl: camera) [verified:
    # disassembly of their caller 0x409f90]
    0x00418890: 'ws_projection', 0x0040a360: 'ws_frustum',
    # the 2D's shader constants and viewport (no arguments) and SetViewport's wrapper (cdecl:
    # D3DVIEWPORT8 *) [verified: disassembly of 0x405650, 0x405ae0]
    0x00405ae0: 'ws_2d_view', 0x00412aa0: 'ws_viewport',
}

# the CRT's _ftol (Ghidra's FID: __ftol), done in place by the generated code
INLINE = {0x0052e3d8: 'ftol'}

# int3s that carry on as under a debugger: the assert's own breakpoint (0x4446f0 ends with it), so
# the assert returns to its caller and the code after each assert call is translated
INT3_CONTINUE = [0x0044470c]
# (the assert returns AL = 0 from its hook, so the inlined `test al, al / je / int3` after each
# call skips its own int3 [verified: disassembly at 0x42d80b])

# Named addresses for the runtime (GAME_<NAME> in gen/image.h)
DATA = {}
