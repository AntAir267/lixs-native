# LEGO Island Xtreme Stunts native: build the runtime and the C generated from the user's own game files.
#   make GAME=<folder with Game.exe, game.dat and CD (the disc's files)>   translate + build build/lixs
# Python with Capstone: the repository's .venv if there is one
PY ?= $(firstword $(wildcard .venv/bin/python) python3)
GAME ?= $(HOME)/.local/share/lixs-native/game
BUILD_CFG ?= builds/lixs.py
# Parallel compiles: each takes ~300 MB; keep it modest so the machine stays usable.
JOBS ?= 6
MAKEFLAGS += -j$(JOBS)
CC = gcc
SDL3 ?= third_party/sdl3
DXVK ?= third_party/dxvk-native
DXVK_INC = -I$(DXVK)/include/dxvk
# AntiStatic (third_party/antistatic, a submodule pinned at a release tag): the translator, the CPU
# core, the Windows layer, DirectMusic 8 and its engine; runtime/ holds only this game's own files
# (port.h, its hooks, the Direct3D 8 bridge and renderer, the films)
TK = third_party/antistatic
TK_INC = -I$(TK)/runtime/win32 -I$(TK)/runtime/cpu -I$(TK)/runtime/rewrite
CFLAGS_COMMON = -std=gnu11 -g -fno-strict-aliasing -frounding-math -fno-omit-frame-pointer -Iruntime $(TK_INC) -Igen -I$(SDL3)/include
CFLAGS_GEN = $(CFLAGS_COMMON) -O2 -w -DCPU_FAST_MEM
CFLAGS_RT = $(CFLAGS_COMMON) -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -Wno-missing-field-initializers
LDFLAGS = -rdynamic -L$(SDL3)/lib -Wl,-rpath,'$$ORIGIN/lib' -lSDL3 -pthread -lm -ldl -lz $(shell pkg-config --libs libavformat libavcodec libswresample libswscale libavutil)

RT_SRC = $(wildcard runtime/*.c)
TK_SRC = $(wildcard $(TK)/runtime/win32/*.c) $(TK)/runtime/cpu/fpu.c $(wildcard $(TK)/runtime/rewrite/*.c) $(TK)/runtime/music/dmusic8.c
TK_HDR = $(wildcard $(TK)/runtime/*/*.h)
# the hand-written C replacing translated functions (runtime/decomp/, DESIGN.md decision 11)
DC_SRC = $(wildcard runtime/decomp/*.c)
RT_OBJ = $(patsubst runtime/%.c,build/obj/rt_%.o,$(RT_SRC)) build/obj/rt_d3d8_gen.o \
         $(foreach f,$(TK_SRC),build/obj/at_$(notdir $(patsubst %/,%,$(dir $(f))))_$(notdir $(f:.c=.o))) \
         $(patsubst runtime/decomp/%.c,build/obj/dc_%.o,$(DC_SRC))
-include gen/files.mk
GEN_SRC = $(GEN_CODE) gen/table.c gen/stubs.c gen/x87_special.c
GEN_OBJ = $(patsubst gen/%.c,build/obj/gen_%.o,$(GEN_SRC))

all: build/lixs build/lib/libSDL3.so.0 build/lib/libdxvk_d3d8.so.0 build/lib/libdxvk_d3d9.so.0

gen/funcs.h: $(wildcard $(TK)/translator/*.py) $(BUILD_CFG)
	$(PY) $(TK)/translator/recomp.py "$(GAME)" $(BUILD_CFG) gen
gen/table.c gen/stubs.c gen/x87_special.c gen/image.h: gen/funcs.h ;
gen/files.mk: gen/funcs.h
	@echo 'GEN_CODE = $(sort $(wildcard gen/code_*.c))' > $@

build/obj/gen_%.o: gen/%.c gen/funcs.h $(wildcard $(TK)/runtime/cpu/*.h) runtime/port.h | build/obj
	$(CC) $(CFLAGS_GEN) -c $< -o $@

build/obj/rt_%.o: runtime/%.c runtime/*.h $(TK_HDR) gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -c $< -o $@

# the toolkit's runtime: build/obj/at_<its folder>_<file>.o
build/obj/at_win32_%.o: $(TK)/runtime/win32/%.c runtime/port.h $(TK_HDR) gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -c $< -o $@
build/obj/at_cpu_%.o: $(TK)/runtime/cpu/%.c runtime/port.h $(TK_HDR) gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -c $< -o $@
build/obj/at_rewrite_%.o: $(TK)/runtime/rewrite/%.c runtime/port.h $(TK_HDR) gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -c $< -o $@
build/obj/at_music_%.o: $(TK)/runtime/music/%.c runtime/port.h $(TK_HDR) gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -DDM_STATIC=1 -I$(DM)/include -c $< -o $@

# (-ffp-contract=off: no fused multiply-adds, so each operation rounds as the original's does)
build/obj/dc_%.o: runtime/decomp/%.c runtime/decomp/*.h runtime/*.h gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -Iruntime -ffp-contract=off -c $< -o $@

# the Direct3D 8 bridge: DXVK's headers for the types, wrappers generated from its d3d8.h
gen/d3d8_gen.c gen/d3d8_gen.h: tools/d3d8gen.py $(DXVK)/include/dxvk/d3d8.h
	$(PY) tools/d3d8gen.py $(DXVK)/include/dxvk/d3d8.h gen/d3d8_gen.c
build/obj/rt_d3d8.o: runtime/d3d8.c runtime/*.h $(TK_HDR) gen/d3d8_gen.h gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) $(DXVK_INC) -Wno-unused-function -c $< -o $@
build/obj/rt_d3d8_gen.o: gen/d3d8_gen.c runtime/*.h $(TK_HDR) gen/d3d8_gen.h | build/obj
	$(CC) $(CFLAGS_RT) $(DXVK_INC) -Wno-unused-function -c $< -o $@

# the shared libraries beside the program (its rpath is $$ORIGIN/lib)
build/lib/%: $(SDL3)/lib/% | build/obj
	mkdir -p build/lib && cp -L $< $@
build/lib/libdxvk_%: $(DXVK)/lib/x86_64-linux-gnu/libdxvk_% | build/obj
	mkdir -p build/lib && cp -L $< $@

# DirectMusic's engine: GothicKit dmusic with TinySoundFont (MIT; third_party/dmusic, li2-native's
# patched copy), the music of runtime/dmusic.c
DM = $(TK)/third_party/dmusic
DM_SRC = $(filter-out %/Thread.Win32.c %/Thread.Posix.c,$(wildcard $(DM)/src/*.c $(DM)/src/*/*.c)) $(DM)/vendor/TinySoundFont/tsf.c
DM_OBJ = $(patsubst $(DM)/%.c,build/obj/dm/%.o,$(DM_SRC))
CFLAGS_DM = -std=gnu11 -O2 -g -w -DDM_STATIC=1 -DDM_BUILD=1 -D_DM_USE_NATIVE_THREAD=1 -I$(DM)/include -I$(DM)/src -I$(DM)/vendor/TinySoundFont
build/obj/dm/%.o: $(DM)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_DM) -c $< -o $@

# (the runtime before the generated code: the translator's weak missing-import stubs (gen/stubs.c)
# and the toolkit's weak defaults (API_WEAK) are both weak, and the linker keeps the first it meets)
build/lixs: $(RT_OBJ) $(DM_OBJ) $(GEN_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

build/obj:
	mkdir -p $@

# Reference build: the original code in Unicorn on the same runtime (ref/ref_cpu.c, tools/lockstep.py).
# Unicorn: a Python unicorn package's library (UNICORN=<its folder>)
UNICORN ?= $(firstword $(wildcard .venv/lib/python3*/site-packages/unicorn))
build/obj/ref_cpu.o: ref/ref_cpu.c runtime/*.h $(TK_HDR) gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -I$(UNICORN)/include -c $< -o $@

build/lixs_ref: $(RT_OBJ) $(DM_OBJ) build/obj/ref_cpu.o $(GEN_OBJ)
	$(CC) -o $@ $^ $(UNICORN)/lib/libunicorn.a $(LDFLAGS) -lrt

# installed for play: the applications menu's "LEGO Island Xtreme Stunts"
PREFIX ?= $(HOME)/.local/opt/lixs-native
APPS ?= $(HOME)/.local/share/applications
install: all
	mkdir -p $(PREFIX)/lib $(APPS)
	cp build/lixs $(PREFIX)/lixs.new && mv -f $(PREFIX)/lixs.new $(PREFIX)/lixs
	cp -L build/lib/libSDL3.so.0 build/lib/libdxvk_d3d8.so.0 build/lib/libdxvk_d3d9.so.0 $(PREFIX)/lib/
	-$(PY) tools/icon.py "$(GAME)/Game.exe" $(PREFIX)/lixs.png
	printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=LEGO Island Xtreme Stunts' \
		'Comment=LEGO Island Xtreme Stunts (2002) recompiled for Linux and Vulkan' \
		'Exec=$(PREFIX)/lixs' 'Icon=$(PREFIX)/lixs.png' 'Categories=Game;' \
		'StartupWMClass=lixs-native' 'Terminal=false' > $(APPS)/lixs-native.desktop
	@echo "installed in $(PREFIX); the applications menu has LEGO Island Xtreme Stunts"

# coverage: the generated code instrumented, the functions a run enters (the toolkit's coverage.c)
COV_GEN_OBJ = $(patsubst gen/%.c,build/cov/gen_%.o,$(GEN_SRC))
build/cov/gen_%.o: gen/%.c gen/funcs.h $(wildcard $(TK)/runtime/cpu/*.h) runtime/port.h
	@mkdir -p build/cov
	$(CC) $(CFLAGS_GEN) -finstrument-functions -c $< -o $@
build/cov/rt_coverage.o: $(TK)/runtime/rewrite/coverage.c
	@mkdir -p build/cov
	$(CC) $(CFLAGS_RT) -DREWRITE_COVERAGE_BUILD -c $< -o $@
build/lixs_cov: $(filter-out build/obj/at_rewrite_coverage.o,$(RT_OBJ)) $(DM_OBJ) build/cov/rt_coverage.o $(COV_GEN_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)
cov: build/lixs_cov build/lib/libSDL3.so.0 build/lib/libdxvk_d3d8.so.0 build/lib/libdxvk_d3d9.so.0
.PHONY: cov

clean:
	rm -rf build gen

.PHONY: all clean
