#!/bin/bash
# Verification helper: renders named shots of the matrix (from Saved/shotlists/<biome>.txt) with a tag and extra args.
# usage: vshots.sh <biome> '<shot regex>' <tag> [extra args...]
#   e.g. vshots.sh temperate '300-(day|gold)' lod3 -dpcvars=r.ForceLOD=3
# Shots: shots/ue-treelab-<shot>-<tag>.png ; timings appended to timings.txt
cd "$(dirname "$0")"
B=$1; RX=$2; TAG=$3; shift 3
declare -A SITE=([temperate]=38500 [boreal]=3753 [rainforest]=5299 [savanna]=21445 [palmcoast]=5299)
list="Saved/shotlists/v-$B-$TAG.txt"
grep -E "^$B-($RX) " "Saved/shotlists/$B.txt" | sed -E "s/^([^ ]+) /\1-$TAG /" > "$list"
[ -s "$list" ] || { echo "no shots"; exit 1; }
log="treelab-v-$B-$TAG"
powershell -NoProfile -ExecutionPolicy Bypass -File shot.ps1 "v-$B-$TAG" -site=${SITE[$B]} -notowns "-shotlist=$(cygpath -w "$PWD/$list")" -fov=50 "$@" > /dev/null 2>&1
n=$(grep -h "LIFESTATS trees total" Saved/Logs/$log.log | head -1 | sed 's/.*trees total=\([0-9]*\).*/\1/')
grep -h "LIFESTATS timing \[" Saved/Logs/$log.log | sed "s/.*timing \[\([^]]*\)\] over 60 frames: /\1 | trees $n | /" | tee -a timings.txt
