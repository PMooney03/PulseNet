$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

docker run --rm -v "${Root}:/work" -w /work ubuntu:24.04 bash -lc "sed -i 's/\r$//' /work/scripts/verify-phase1.sh && bash /work/scripts/verify-phase1.sh"
