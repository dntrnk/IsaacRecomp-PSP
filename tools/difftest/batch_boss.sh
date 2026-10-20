#!/bin/bash
# batch_boss.sh <game> <ticks> <razor> <templates...>: each boss room template on its floor, auto-aim + random movement
# Prints per template: whether the boss died (_root.bossd in Ruffle), the ball types seen, and the first difference.
cd "$(dirname "$0")"; G=$1; PER=$2; RZ=$3; shift 3; [ "$G" = eternal ] && export START_SCRIPT=$PWD/start_eternal.txt
mkdir -p bb; O=${OUT:-out}
for T in "$@"; do
  python3 gen_boss.py $T 7 $PER $RZ > bb/${G}_$T.txt
  N=$(tail -1 bb/${G}_$T.txt | cut -d' ' -f1)
  ./cmp.sh $G bb/${G}_$T.txt $((N+2)) >/dev/null 2>&1
  R=$O/${G}_${T}_${G}_ruffle.txt; C=$O/${G}_${T}_${G}_recomp.txt
  echo "$G $T: bossd=$(grep "_root.bossd" $R | awk '{print $4}') types=$(grep -o " s=[0-9]* " $R | sort -u | tr -d ' ' | tr '\n' ',') :: $(python3 cmpf.py $R $C 1 | head -2 | tr '\n' ' ')"
done
