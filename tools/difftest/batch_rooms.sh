#!/bin/bash
# batch_rooms.sh <game> <seed> <floors...>: 12 random rooms per floor (teleport), 300 ticks each, god mode
cd "$(dirname "$0")"; G=$1; [ "$G" = eternal ] && export START_SCRIPT=$PWD/start_eternal.txt; SEED=$2; shift 2; mkdir -p rr
for K in "$@"; do
  python3 gen_room.py $((SEED+K)) 0,0,0,0,0,0,0,0,0,0,0,0 300 $K > rr/r${G}${K}.txt
  N=$(tail -1 rr/r${G}${K}.txt | cut -d' ' -f1)
  ./cmp.sh $G rr/r${G}${K}.txt $((N+2)) >/dev/null 2>&1
  echo "$G floor+$K: $(python3 cmpf.py ${OUT:-out}/r${G}${K}_${G}_ruffle.txt ${OUT:-out}/r${G}${K}_${G}_recomp.txt 1 | head -2 | tr '\n' ' ')"
done
