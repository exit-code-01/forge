# CI smoke test (Windows). Mirror of run_sandbox.sh:
#   exit 0                        -> a display was present, ran and shut down
#   exit 1 with "glfwInit failed" -> headless runner, graceful failure path
# Anything else is a real failure.
$ErrorActionPreference = "Continue"

$bin = "build\bin\forge_sandbox.exe"

$out = & $bin 2>&1 | Out-String
$code = $LASTEXITCODE
Write-Host $out

if ($code -eq 0) {
    Write-Host "PASS: sandbox ran and exited cleanly (display present)."
    exit 0
}

if ($code -eq 1 -and $out -match "glfwInit failed") {
    Write-Host "PASS: headless runner, sandbox failed gracefully as designed."
    exit 0
}

Write-Host "FAIL: unexpected exit ($code) without the expected graceful GLFW error."
exit 1
