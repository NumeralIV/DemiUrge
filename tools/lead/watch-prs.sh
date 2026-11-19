#!/usr/bin/env bash
# Lead tool: wait until a helper opens, updates or comments on a pull request, then print it and exit.
# Usage: watch-prs.sh [poll-seconds]   (state kept in Saved-style file outside git)
GH="$LOCALAPPDATA/Microsoft/WinGet/Packages/GitHub.cli_Microsoft.Winget.Source_8wekyb3d8bbwe/bin/gh.exe"
STATE="$LOCALAPPDATA/demiurge-pr-watch.state"
POLL="${1:-300}"
snap() { "$GH" pr list -R NumeralIV/DemiUrge --state open --json number,headRefOid,title,isDraft \
  --jq 'sort_by(.number) | .[] | "\(.number)\t\(.headRefOid)\t\(.isDraft)\t\(.title)"' 2>/dev/null; }
[ -f "$STATE" ] || snap > "$STATE"
while true; do
  cur=$(snap) || { sleep "$POLL"; continue; }
  if [ "$cur" != "$(cat "$STATE")" ]; then
    new=$(diff <(cat "$STATE") <(echo "$cur") | grep '^>' | cut -c3-)
    echo "$cur" > "$STATE"
    if [ -n "$new" ]; then      # opened or updated (a PR that merely closed is not news)
      echo "PR activity on NumeralIV/DemiUrge:"
      echo "$new"
      exit 0
    fi
  fi
  sleep "$POLL"
done
