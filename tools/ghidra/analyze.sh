#!/bin/sh
# Once: import and analyse the user's Game.exe into work/ghidra (project "lixs"), then decompile
# every function into work/ghidra/all/<addr>.c with index.tsv (address, size, name).
R=$(cd "$(dirname "$0")/../.." && pwd)
G=${1:-$HOME/.local/share/lixs-native/game}
mkdir -p "$R/work/ghidra"
cd "$R/work/ghidra" && MAXMEM=6G ~/.local/opt/ghidra_12.1.4_PUBLIC/support/analyzeHeadless "$R/work/ghidra" lixs \
  -import "$(readlink -f "$G/Game.exe")" -overwrite -scriptPath "$R/tools/ghidra" \
  -postScript DumpAll.java "$R/work/ghidra/all" > analyze.log 2>&1
