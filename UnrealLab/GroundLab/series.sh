#!/bin/bash
# series.sh <prefix> <lat> <lon> <heading> <sunElev,sunAz> [extra args...]  -- five altitudes, orbit to ~300 m
cd "$(dirname "$0")"
P=$1; LAT=$2; LON=$3; HD=$4; SUN=$5; shift 5
for v in "1 400,90,40" "2 90,62,50" "3 18,40,50" "4 3.5,22,55" "5 0.85,20,60"; do
  set -- $v "$@"; N=$1; L=$2; shift 2
  if [ -n "$ONLY" ] && [[ " $ONLY " != *" $N "* ]]; then continue; fi
  IFS=, read D PI FOV <<< "$L"
  powershell -NoProfile -ExecutionPolicy Bypass -File shot.ps1 $P-$N -look=$LAT,$LON,$D,$HD,$PI,$FOV -sunl=$SUN "$@" 2>&1 | grep -a "$P-$N :"
  grep -a "STATS" Saved/Logs/ground-$P-$N.log | sed 's/.*STATS/STATS/' | cut -c1-260
done
