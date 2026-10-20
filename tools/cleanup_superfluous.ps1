#Requires -Version 5.1
# Finish repo cleanup: promote SecureConnect body, delete milliken_extract,
# remove any residual *.vcxproj / filters at repo root.
# Run from repository root:
#   powershell -ExecutionPolicy Bypass -File tools\cleanup_superfluous.ps1

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot\..

Write-Host "== git pull =="
git pull --ff-only

# 1) Promote SecureConnect implementation before deleting the extract
if (Test-Path "milliken_extract\ksnet.cpp") {
    Write-Host "== promote SecureConnect body -> src/engine/network/ksnet/ksnet.cpp =="
    Copy-Item -Force "milliken_extract\ksnet.cpp" "src\engine\network\ksnet\ksnet.cpp"
    git add "src\engine\network\ksnet\ksnet.cpp"
}

# 2) Remove staging extract (all contents)
if (Test-Path "milliken_extract") {
    Write-Host "== remove milliken_extract/ =="
    git rm -rf milliken_extract
}

# 3) Residual VS noise at root (should already be gone on master)
$noise = Get-ChildItem -File -Path . -Include *.vcxproj,*.vcxproj.filters -ErrorAction SilentlyContinue
foreach ($f in $noise) {
    Write-Host "== remove $($f.Name) =="
    git rm -f --ignore-unmatch $f.Name
}

if (Test-Path "backups") {
    git rm -rf backups
}

Write-Host "== status =="
git status --short

$staged = git diff --cached --name-only
if (-not $staged) {
    Write-Host "Nothing to commit."
    exit 0
}

git commit -m @"
chore: finish cleanup — drop milliken_extract, ship SecureConnect body

Promotes milliken_extract/ksnet.cpp into src/engine/network/ksnet/ksnet.cpp
(SecureConnect + SetPrivateKey XOR path) then removes the staging tree.
"@

Write-Host "== push =="
git push origin HEAD
Write-Host "Done."
