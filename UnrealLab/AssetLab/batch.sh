#!/bin/bash
# usage: batch.sh "name args..." "name args..."   (runs AssetLab shots sequentially)
cd "$(dirname "$0")"
for spec in "$@"; do
  powershell -NoProfile -ExecutionPolicy Bypass -File shot.ps1 $spec 2>&1 | grep -E " : |ASSETSHOW|timing|Error" | head -14
done
