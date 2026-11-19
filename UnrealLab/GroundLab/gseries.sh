#!/bin/bash
# gseries.sh <prefix> <lat> <lon> <heading> [extra args]  -- ground series at 20, 10, 5, 2 km altitude (pitch 60, fov 50)
# ONLY="10 5" limits the altitudes. Sun 32 deg high from the camera's left. Shots: shots/ue-groundlab-<prefix>-<alt>.png
cd "$(dirname "$0")"
P=$1; LAT=$2; LON=$3; HD=$4; shift 4
SAZ=$(python -c "print(($HD - 95) % 360)")
for A in ${ONLY:-20 10 5 2}; do
  D=$(python -c "import math; print(round($A / math.sin(math.radians(60)), 3))")
  powershell -NoProfile -ExecutionPolicy Bypass -File shot.ps1 $P-$A -look=$LAT,$LON,$D,$HD,60,50 -sunl=32,$SAZ -ev=-1 -apscale=0.25 "$@" 2>&1 | grep -a "$P-$A :"
  grep -a "STATS" Saved/Logs/ground-$P-$A.log | sed 's/.*STATS/STATS/' | cut -c1-110
done
