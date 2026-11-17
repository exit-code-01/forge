#!/bin/bash
# usage: tools/dev/rundev.sh <seconds> [cap.png]
# Dev-mode run (FORGE_DEV file console + FORGE_AUTOPLAY), log to build/_dev.log.
# The engine reads build/_cmd.lua (file-drop console); build/_start.lua is copied
# there first if present. Run under Git Bash. Env overrides:
#   FORGE_EXE  path to exe (default build/debug/bin/forge_sandbox.exe)
cd "$(dirname "$0")/../.." || exit 1
EXE="${FORGE_EXE:-build/debug/bin/forge_sandbox.exe}"
taskkill //F //IM forge_sandbox.exe >/dev/null 2>&1
rm -f build/_cmd.lua
[ -f build/_start.lua ] && cp build/_start.lua build/_cmd.lua
FORGE_DEV=1 FORGE_AUTOPLAY=1 "$EXE" > build/_dev.log 2>&1 &
sleep ${1:-8}
[ -n "$2" ] && python tools/dev/gui.py cap "$2"
taskkill //F //IM forge_sandbox.exe >/dev/null 2>&1
grep -v TRACE build/_dev.log | grep -i -E "error|warn|fatal|vuid|script " | head -20
