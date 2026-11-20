#!/bin/bash
# TreeLab shot matrix: biome forests at 50 m / 300 m / 1.2 km / 5 km, by day and at golden hour.
# One editor process per biome (world built once, -shotlist mode of LabGameMode).
# usage: matrix.sh [biome-regex] [alt-light regex] [extra args...]   e.g. matrix.sh 'temperate|savanna' '300-day|1200-gold' -gentrees
# Shots: shots/ue-treelab-<biome>-<alt>-<day|gold>[-gen].png ; timings appended to UnrealLab/TreeLab/timings.txt
cd "$(dirname "$0")"
BFILTER="${1:-.}"; SFILTER="${2:-.}"; shift; shift
EXTRA="$*"
SUFFIX=""; [[ "$EXTRA" == *gentrees* ]] && SUFFIX="-gen"
# biome site focus_x focus_y cam_yaw
BIOMES=(
 "temperate 38500 -2550 -750 30"
 "boreal 3753 -1050 450 300"
 "rainforest 5299 -1050 750 30"
 "savanna 21445 -1050 -1350 30"
 "palmcoast 5299 -2550 -150 120"
)
ALTS=("50 -7" "300 -20" "1200 -32" "5000 -50")
mkdir -p Saved/shotlists
for b in "${BIOMES[@]}"; do
  set -- $b; name=$1; site=$2; fx=$3; fy=$4; yaw=$5
  [[ "$name" =~ $BFILTER ]] || continue
  list="Saved/shotlists/$name$SUFFIX.txt"; : > "$list"
  for a in "${ALTS[@]}"; do
    set -- $a; alt=$1; pitch=$2
    for light in day gold; do
      [[ "$alt-$light" =~ $SFILTER ]] || continue
      d=$(python -c "import math;print(round($alt/math.tan(math.radians(${pitch#-})),1))")
      cx=$(python -c "import math;print(round($fx-math.cos(math.radians($yaw))*$d,1))")
      cy=$(python -c "import math;print(round($fy-math.sin(math.radians($yaw))*$d,1))")
      if [ $light = day ]; then sun="$(( (yaw + 240) % 360 )),48"; ev=0; else sun="$(( (yaw + 285) % 360 )),6"; ev=1.4; fi
      clouds=1; [ $alt = 5000 ] && clouds=0
      echo "$name-$alt-$light$SUFFIX $cx,$cy,$alt,$yaw,$pitch $sun $ev $clouds" >> "$list"
    done
  done
  [ -s "$list" ] || continue
  log="treelab-m-$name$SUFFIX"
  powershell -NoProfile -ExecutionPolicy Bypass -File shot.ps1 "m-$name$SUFFIX" -site=$site -notowns "-shotlist=$(cygpath -w "$PWD/$list")" -fov=50 $EXTRA > /dev/null 2>&1
  n=$(grep -h "LIFESTATS trees total" Saved/Logs/$log.log | head -1 | sed 's/.*trees total=\([0-9]*\).*/\1/')
  grep -h "LIFESTATS timing \[" Saved/Logs/$log.log | sed "s/.*timing \[\([^]]*\)\] over 60 frames: /\1 | trees $n | /" | tee -a timings.txt
done
