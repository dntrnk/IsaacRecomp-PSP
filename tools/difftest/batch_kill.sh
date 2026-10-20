#!/bin/bash
# batch_kill.sh <game> <ticks> <types...>: each enemy type in the first room, auto-aimed at until it dies (deaths,
# splits, drops). Prints the number of living enemies at the end (Ruffle) and the first difference.
cd "$(dirname "$0")"; G=$1; N=$2; shift 2; [ "$G" = eternal ] && export START_SCRIPT=$PWD/start_eternal.txt
mkdir -p kk; O=${OUT:-out}
for T in "$@"; do
  python3 gen_kill.py $T $T $N > kk/${G}_k$T.txt
  ./cmp.sh $G kk/${G}_k$T.txt $((1002+N)) >/dev/null 2>&1
  R=$O/${G}_k${T}_${G}_ruffle.txt; C=$O/${G}_k${T}_${G}_recomp.txt
  L=$(grep "^\[ball\] $((999+N)) " $R | grep -v "dones=true" | grep -c " s=[1-9][0-9][0-9]* .* hp=[0-9.]*[1-9]")
  echo "$G k$T: alive_end=$L :: $(python3 cmpf.py $R $C 1 | head -2 | tr '\n' ' ')"
done
