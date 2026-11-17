# Smoke test (Windows), for LOCAL use on a machine with a GPU. CI does not run
# this (hosted runners have no GPU). Starts the exe, lets it run for $Seconds,
# kills that PID only, and fails if it exited early with a non-zero code.
#   $env:BIN overrides the binary (default: release preset).
param([int]$Seconds = 15)
$bin = if ($env:BIN) { $env:BIN } else { "build\release\bin\forge_sandbox.exe" }
$p = Start-Process -FilePath $bin -PassThru
Start-Sleep -Seconds $Seconds
if ($p.HasExited) {
    if ($p.ExitCode -eq 0) { Write-Host "PASS: exited cleanly."; exit 0 }
    Write-Host "FAIL: exited early with code $($p.ExitCode)."; exit 1
}
Stop-Process -Id $p.Id -Force
Write-Host "PASS: still running after $Seconds s."
exit 0
