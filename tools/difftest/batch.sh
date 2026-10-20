#!/bin/bash
# batch.sh <game> <types...>: one enemy type at a time in the first room, 600 ticks of random input
cd "$(dirname "$0")"; G=$1; [ "$G" = eternal ] && export START_SCRIPT=$PWD/start_eternal.txt; shift; mkdir -p en
for T in "$@"; do
  python3 gen_en.py $T $T 600 > en/t${T}.txt
  ./cmp.sh $G en/t${T}.txt 1601 >/dev/null 2>&1
  echo "$G t$T: $(python3 cmpf.py ${OUT:-out}/t${T}_${G}_ruffle.txt ${OUT:-out}/t${T}_${G}_recomp.txt 1 | head -2 | tr '\n' ' ')"
done
