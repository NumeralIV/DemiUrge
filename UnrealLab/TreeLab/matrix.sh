#!/bin/bash
# TreeLab shot matrix: biome forests at 50 m / 300 m / 1.2 km / 5 km, by day and at golden hour.
# usage: matrix.sh [filter-regex] [extra args...]   e.g. matrix.sh 'temperate-300' -gentrees
# Shots: shots/ue-treelab-<biome>-<alt>-<day|gold>[<suffix>].png ; timings appended to UnrealLab/TreeLab/timings.txt
cd "$(dirname "$0")"
FILTER="${1:-.}"; shift
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
for b in "${BIOMES[@]}"; do
  set -- $b; name=$1; site=$2; fx=$3; fy=$4; yaw=$5
  for a in "${ALTS[@]}"; do
    set -- $a; alt=$1; pitch=$2
    for light in day gold; do
      tag="$name-$alt-$light$SUFFIX"
      [[ "$tag" =~ $FILTER ]] || continue
      d=$(python -c "import math;print(round($alt/math.tan(math.radians(${pitch#-})),1))")
      cx=$(python -c "import math;print(round($fx-math.cos(math.radians($yaw))*$d,1))")
      cy=$(python -c "import math;print(round($fy-math.sin(math.radians($yaw))*$d,1))")
      if [ $light = day ]; then sun="$(( (yaw + 240) % 360 )),48"; else sun="$(( (yaw + 285) % 360 )),6"; fi
      clouds=""; [ $alt = 5000 ] && clouds="-noclouds"
      powershell -NoProfile -ExecutionPolicy Bypass -File shot.ps1 "$tag" -site=$site -notowns "-cam=$cx,$cy,$alt,$yaw,$pitch" "-sun=$sun" -fov=50 $clouds $EXTRA > /dev/null 2>&1
      t=$(grep -h "timing over" Saved/Logs/treelab-$tag.log | sed 's/.*timing over 60 frames: //')
      n=$(grep -h "LIFESTATS trees total" Saved/Logs/treelab-$tag.log | head -1 | sed 's/.*trees total=\([0-9]*\).*/\1/')
      echo "$tag | trees $n | $t" | tee -a timings.txt
    done
  done
done
