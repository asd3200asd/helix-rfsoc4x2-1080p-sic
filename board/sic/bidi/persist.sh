#!/usr/bin/env bash
# Claude 2026-11-13: copy finished bidi runs from RAM to the SD card.  During a run sic/bidi/<run> is a symlink to /dev/shm/bidi/<run>
# (bidi_run.sh), so nothing in the real-time path waits for the SD card.  usage: bash sic/bidi/persist.sh [run]   (no run = all)
W=/home/xilinx/claude_nlsg_20261110
for l in $W/sic/bidi/*; do [ -L "$l" ] || continue; t=$(readlink "$l"); case "$t" in /dev/shm/bidi/*) ;; *) continue ;; esac
  [ -n "${1:-}" ] && [ "$(basename "$l")" != "$1" ] && continue
  if [ ! -d "$t" ]; then sudo rm -f "$l"; continue; fi
  sudo rm -rf "$l.persist"; sudo cp -a "$t" "$l.persist" && sudo rm -f "$l" && sudo mv "$l.persist" "$l" && sudo rm -rf "$t" && echo "persisted $(basename "$l")"
done
