#!/bin/bash
# cmp.sh <og|wotl|eternal> <script> <frames>: runs the same test script in Ruffle (the HD-patched SWF) and in the
# recomp, with the same random seed and a virtual clock, and writes both outputs to $OUT (see README.md).
G=$1; S=$2; N=$3; SEED=${SEED:-12345}
: "${HARNESS:?path to target/release/isaac_harness}" "${RECOMP:?path to the recomp's isaac binary}"
case $G in og) SWF=$SWF_OG; EXE=$EXE_OG;; eternal) SWF=$SWF_ETERNAL; EXE=$EXE_ETERNAL;; wotl) SWF=$SWF_WOTL; EXE=$EXE_WOTL;; esac
OUT=${OUT:-out}; mkdir -p "$OUT"
B=$(basename "$S" .txt)
SAVE=$(mktemp -u /tmp/difftest_XXXXXX.sol)
ISAAC_AVMRNG=$SEED timeout 1200 "$HARNESS" "$SWF" "$S" "$N" 2>/dev/null | grep "^\[" > "$OUT/${B}_${G}_ruffle.txt"
ISAAC_AVMRNG=$SEED ISAAC_NOMDM=1 timeout 1200 "$RECOMP" --headless --norender --save "$SAVE" --script "$S" --frames "$N" --game "$G" "$EXE" 2>&1 \
  | grep "^\[\(ball\|eval\|call\|rng\|prop\)\]" > "$OUT/${B}_${G}_recomp.txt"
rm -f "$SAVE"
wc -l "$OUT/${B}_${G}_ruffle.txt" "$OUT/${B}_${G}_recomp.txt"
