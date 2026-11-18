#!/bin/sh
# The route used for baselines: menus, new game, intro, the first stunt, free roam (4200 frames).
# usage: route1.sh <binary> <outdir>
B=$1; O=$2; mkdir -p "$O/user"
LIXS_KEYS="900:SPACE,1200:RETURN,1500:RETURN,1800:RETURN,2100:SPACE,2400:UP*600,3100:SPACE,3400:RETURN" \
timeout -s KILL 900 "$B" --test --user "$O/user" --frames 4200 --hash "$O/hash.txt" --hash-every 50 \
  --shot 880 "$O/f880.png" --shot 2300 "$O/f2300.png" --shot 3500 "$O/f3500.png" --shot 4190 "$O/f4190.png" \
  > "$O/stdout.txt" 2>&1
echo "exit $?" >> "$O/stdout.txt"
