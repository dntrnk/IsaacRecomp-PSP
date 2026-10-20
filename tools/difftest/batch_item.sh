#!/bin/bash
# batch_item.sh <game> <ticks> <items...>: each item picked up from a pedestal, then fights with auto-aim, the
# active used every 90 ticks; prints whether it was picked up (_root.ups[item] in Ruffle) and the first difference
cd "$(dirname "$0")"; G=$1; N=$2; shift 2
if [ "$G" = eternal ]; then export START_SCRIPT=$PWD/start_eternal.txt; fi
mkdir -p ii; O=${OUT:-out}
for I in "$@"; do
  python3 gen_item.py $I $I $N > ii/${G}_i$I.txt
  ./cmp.sh $G ii/${G}_i$I.txt $((1002+N)) >/dev/null 2>&1
  R=$O/${G}_i${I}_${G}_ruffle.txt; C=$O/${G}_i${I}_${G}_recomp.txt
  echo "$G i$I: ups=$(grep "_root.ups $I = " $R | tail -1 | awk '{print $5}') :: $(python3 cmpf.py $R $C 1 | head -2 | tr '\n' ' ')"
done
