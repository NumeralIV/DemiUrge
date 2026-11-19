#!/bin/bash
# usage: batch.sh "name args..." "name args..."   (runs shots sequentially)
cd "$(dirname "$0")"
for spec in "$@"; do
  powershell -NoProfile -ExecutionPolicy Bypass -File shot.ps1 $spec 2>&1 | grep -i "timing\|NO SHOT\| : \|trees total" | head -3
done
