#!/usr/bin/env bash
# CI smoke test (POSIX). The sandbox opens a window and loops until closed,
# so on a headless runner it can't reach a clean exit 0 — GLFW fails to init
# instead. Both outcomes are correct:
#   exit 0                          -> a display was present, ran and shut down
#   exit 1 with "glfwInit failed"   -> headless runner, graceful failure path
# Anything else (crash, wrong error) is a real failure.
set -u

BIN="build/bin/forge_sandbox"

out=$("$BIN" 2>&1)
code=$?
echo "$out"

if [ "$code" -eq 0 ]; then
    echo "PASS: sandbox ran and exited cleanly (display present)."
    exit 0
fi

if [ "$code" -eq 1 ] && echo "$out" | grep -q "glfwInit failed"; then
    echo "PASS: headless runner, sandbox failed gracefully as designed."
    exit 0
fi

echo "FAIL: unexpected exit ($code) without the expected graceful GLFW error."
exit 1
