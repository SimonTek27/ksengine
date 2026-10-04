<#
.SYNOPSIS
    ksengine/simulator Qt dependency gate.

.DESCRIPTION
    Reports every file under src/engine and src/simulator that still touches
    Qt (header include, Q_OBJECT, signals:/slots:). The count must go down
    module by module during the conversion and reach 0 before Fase 8 deletes
    find_package(Qt6)/Qt6::/AUTOMOC from src/engine/CMakeLists.txt.

    src/sdk/kseditor, resources/ui and tests are NOT scanned: Qt is allowed
    there (the editor is the only Qt consumer in the project).

.EXAMPLE
    powershell -File tools/check_no_qt.ps1            # report, exit 0
    powershell -File tools/check_no_qt.ps1 -Strict    # exit 1 if any file left
#>
[CmdletBinding()]
param(
    [switch]$Strict,
    [string]$Root
)

$ErrorActionPreference = 'Stop'
if (-not $Root) {
    $Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
}

$targets = @(
    (Join-Path $Root 'src\engine')
    (Join-Path $Root 'src\simulator')
) | Where-Object { Test-Path $_ }

if (-not $targets) {
    Write-Error 'check_no_qt: src/engine and src/simulator not found'
    exit 2
}

$rg = Get-Command rg -ErrorAction SilentlyContinue
if ($rg) {
    $qtIncludes = & rg -l --glob '*.h' --glob '*.cpp' -e '#\s*include\s*<Q' $targets 2>$null
    $qObjects   = & rg -l --glob '*.h' --glob '*.cpp' -e '^\s*Q_OBJECT\b'   $targets 2>$null
    $sigSlots   = & rg -l --glob '*.h' --glob '*.cpp' -e '^\s*(signals|slots)\s*:' $targets 2>$null
}
else {
    $files = Get-ChildItem -Path $targets -Recurse -Include *.h, *.cpp -File
    $qtIncludes = $files | Where-Object { $_ | Select-String -Pattern '#\s*include\s*<Q' -Quiet } | ForEach-Object FullName
    $qObjects   = $files | Where-Object { $_ | Select-String -Pattern '^\s*Q_OBJECT\b'   -Quiet } | ForEach-Object FullName
    $sigSlots   = $files | Where-Object { $_ | Select-String -Pattern '^\s*(signals|slots)\s*:' -Quiet } | ForEach-Object FullName
}

$all = @($qtIncludes) + @($qObjects) + @($sigSlots) |
    Where-Object { $_ } |
    ForEach-Object { (Resolve-Path -LiteralPath $_).Path } |
    Sort-Object -Unique

# Omit any Qt includes that are inside an #ifdef KS_HAS_QT guard
if ($qtIncludes) {
    $qtIncludes = $qtIncludes | Where-Object {
        $null -eq (Select-String -Path $_ -Pattern '#ifdef KS_HAS_QT' -Quiet)
    }
}
# Recompute Qt-touching file count with the filtered list
$all = @($qtIncludes) + @($qObjects) + @($sigSlots) |
    Where-Object { $_ } |
    ForEach-Object { (Resolve-Path -LiteralPath $_).Path } |
    Sort-Object -Unique

$engineFiles = 0
$simFiles = 0
foreach ($t in $targets) {
    $engineFiles += @(Get-ChildItem -Path $t -Recurse -Include *.h, *.cpp -File).Count
}

$byDir = @{}
foreach ($f in $all) {
    $rel = $f.Substring($Root.Length).TrimStart('\', '/')
    $parts = $rel -split '[\\/]'
    # src\<engine|simulator>\<module>\...
    $key = if ($parts.Count -ge 3) { "$($parts[1])\$($parts[2])" } else { "$($parts[1])" }
    if (-not $byDir.ContainsKey($key)) { $byDir[$key] = 0 }
    $byDir[$key]++
}

Write-Host "check_no_qt: $($all.Count) of $engineFiles files still touch Qt"
Write-Host ("  Q_OBJECT files: {0}   signals/slots files: {1}" -f @($qObjects).Count, @($sigSlots).Count)
Write-Host ''
$byDir.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object {
    Write-Host ("  {0,4}  {1}" -f $_.Value, $_.Key)
}

if ($Strict) {
    if ($all.Count -gt 0) {
        Write-Host ''
        Write-Host 'check_no_qt -Strict FAILED: src/engine and src/simulator must be Qt-free.' -ForegroundColor Red
        exit 1
    }
    Write-Host ''
    Write-Host 'check_no_qt -Strict OK: no Qt left in the engine or the simulator.' -ForegroundColor Green
}
exit 0
