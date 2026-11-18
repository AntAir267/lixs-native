# Third-party code

- `antistatic/`: the [AntiStatic](https://github.com/AntAir267/AntiStatic-Recomp-Toolkit) recompilation
  toolkit (Unlicense), a git submodule pinned at a release tag: the translator, the CPU core, the
  Windows layer, DirectInput, DirectSound, DirectMusic 8 and its music engine (GothicKit dmusic with
  TinySoundFont, MIT). After cloning: `git submodule update --init`.
- `sdl3/` (not committed): SDL 3.4.18 (zlib licence), built as a shared library and installed here:

      cmake -S SDL-3.4.18 -B sdl3-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/sdl3
      cmake --build sdl3-build && cmake --install sdl3-build

- `dxvk-native/` (not committed): DXVK 3.1.1 (zlib licence) built natively, with its Direct3D 8 front
  end and SDL3 as its window system, with AntiStatic's DXVK patches. This port's set is all five
  (soft-shadows depth-texture live-options texbem windowed-gamma); its test baselines hold for that set:

      git clone --recursive --branch v3.1.1 https://github.com/doitsujin/dxvk
      antistatic/tools/d3d9/build_dxvk.sh --src dxvk --sdl3 $PWD/sdl3 --prefix $PWD/dxvk-native --d3d8 \
          --patches "soft-shadows depth-texture live-options texbem windowed-gamma"

  `make` copies libdxvk_d3d8 and libdxvk_d3d9 into build/lib beside the program.
