/* port.h: LEGO Island Xtreme Stunts as the AntiStatic runtime sees it (docs/MOVING.md). Values
   describe the game as the program sees it under Windows; none of it is the game's code or data. */
#pragma once
#define PORT_TITLE "LEGO Island Xtreme Stunts"
#define PORT_SLUG "lixs-native"
#define PORT_ENV "LIXS"                     /* LIXS_KEYS, LIXS_PAD, ... (the port's test scripts) */
#define PORT_VROOT "C:\\LIXS"
#define PORT_START_DIR ""
#define PORT_EXE "Game.exe"
/* AutoRun.exe starts Game.EXE with the CD's two data roots, raw (LIXS\CD\) and compressed
   (LIXS\CDC\); the game quits without both [verified: AutoRun.exe's strings; 0x4014a0, 0x4091a4] */
#define PORT_CMDLINE_ARGS "\"D:\\LIXS\\CD\\\" \"D:\\LIXS\\CDC\\\""
#define PORT_VOLUME_LABEL "SYSTEM"
/* the disc in D: (<game>/CD), its real label [verified: the ISO's volume descriptor]; the game
   never asks for labels (no GetVolumeInformation import) */
#define PORT_CD_LABEL "LIXSRGN2"
#define PORT_ARENA_START 0x10800000u
#define PORT_SCREEN_W 640                   /* (the screen it saw before the move) */
#define PORT_SCREEN_H 480
#define PORT_TEST_IDLE_MS 1.0               /* (its baselines: an empty wait lets 1 ms pass) */
#define PORT_TEST_SCHEDULER 2               /* the C++ line's, with lixs's two fixes */
/* DirectMusic 8 (runtime/music/dmusic8.c): segments are matched by their bytes against the .sgt
   files under this folder (zone folders reuse file names with different contents) */
#define PORT_DMUSIC_SEARCH_DIR "D:\\LIXS\\CD"
