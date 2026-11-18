#!/bin/sh
# usage: decomp.sh addr1,addr2,...   -> work/ghidra/decomp/<entry>.c
# (work/ghidra/all/ holds every function, made once with DumpAll.java: tools/ghidra/analyze.sh)
R=$(cd "$(dirname "$0")/../.." && pwd)
cd "$R/work/ghidra" && MAXMEM=5G ~/.local/opt/ghidra_12.1.4_PUBLIC/support/analyzeHeadless "$R/work/ghidra" lixs \
  -process Game.exe -noanalysis -readOnly -scriptPath "$R/tools/ghidra" \
  -postScript DecompContaining.java "$R/work/ghidra/decomp" "$1" > decomp.log 2>&1
grep -E "ERROR|no function" decomp.log | grep -v VarnodeContext | head
