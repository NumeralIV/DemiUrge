#!/bin/bash
# ab.sh: GPU cost of the ground block, same views with and without it, interleaved (other GPU users add noise)
cd "$(dirname "$0")"
for rep in 1 2; do
for v in "farm 34.36 158.1 0 10" "farm 34.36 158.1 0 2" "edge 39.455 158.828 0 5" "coast 34.93 168.6 90 2"; do
  set -- $v; D=$(python -c "import math; print(round($5 / math.sin(math.radians(60)), 3))")
  for M in M_SurfLand M_SurfLandNoGround; do
    powershell -NoProfile -ExecutionPolicy Bypass -File shot.ps1 ab-$1-$5-$M -look=$2,$3,$D,$4,60,50 -sunl=32,270 -ev=-1 -apscale=0.25 -landmat=/Game/Surface/$M.$M > /dev/null 2>&1
    echo "$rep $1-$5 $M $(grep -a STATS Saved/Logs/ground-ab-$1-$5-$M.log | sed 's/.*STATS //' | cut -c1-80)"
  done
done; done
