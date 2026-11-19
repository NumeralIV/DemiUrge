#!/usr/bin/env bash
# Lead tool: wait until a helper opens, updates or comments on a pull request, then print it and exit.
# Usage: watch-prs.sh [poll-seconds]   (state kept in Saved-style file outside git)
GH="$LOCALAPPDATA/Microsoft/WinGet/Packages/GitHub.cli_Microsoft.Winget.Source_8wekyb3d8bbwe/bin/gh.exe"
STATE="$LOCALAPPDATA/demiurge-pr-watch.state"
POLL="${1:-300}"
snap() { "$GH" pr list -R NumeralIV/DemiUrge --state open --json number,headRefOid,updatedAt,title,isDraft \
  --jq 'sort_by(.number) | .[] | "\(.number)\t\(.headRefOid)\t\(.updatedAt)\t\(.isDraft)\t\(.title)"' 2>/dev/null; }
[ -f "$STATE" ] || snap > "$STATE"
while true; do
  cur=$(snap) || { sleep "$POLL"; continue; }
  if [ "$cur" != "$(cat "$STATE")" ]; then
    echo "PR activity on NumeralIV/DemiUrge:"
    diff <(cat "$STATE") <(echo "$cur") | grep '^>' | cut -c3-
    echo "$cur" > "$STATE"
    exit 0
  fi
  sleep "$POLL"
done
