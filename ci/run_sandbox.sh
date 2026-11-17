#!/usr/bin/env bash
# Smoke test (POSIX), local or CI. Only for machines WITHOUT a GPU/display:
# the sandbox either fails gracefully in glfwInit (exit 1) or, with a display,
# never exits on its own, so it is bounded by `timeout` (124 = still running).
#   BIN=path/to/forge_sandbox overrides the binary (default: release preset).
set -u

BIN="${BIN:-build/release/bin/forge_sandbox}"

out=$(timeout 60 "$BIN" 2>&1)
code=$?
echo "$out" | tail -40

if [ "$code" -eq 0 ] || [ "$code" -eq 124 ]; then
    echo "PASS: sandbox started ($code; 124 = ran until the timeout)."
    exit 0
fi

if [ "$code" -eq 1 ] && echo "$out" | grep -q "glfwInit failed"; then
    echo "PASS: headless runner, sandbox failed gracefully as designed."
    exit 0
fi

echo "FAIL: unexpected exit ($code) without the expected graceful GLFW error."
exit 1
