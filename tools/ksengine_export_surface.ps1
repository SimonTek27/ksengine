<#
.SYNOPSIS
  Measures the export surface ksengine.dll has to provide: the symbols that
  (a) some consumer object file references and (b) ksengine.lib defines.

.DESCRIPTION
  ksengine.dll only ships what is annotated KSENGINE_API (src/engine/KsExport.h),
  so the annotation pass has to cover exactly the entities behind these
  symbols. Both halves come from dumpbin:

    consumer .obj : ... UNDEF  notype ()    External     | ?mangled
    ksengine.lib  : ... SECTn  notype       External     | ?mangled

  Intersecting the two leaves only what must cross the DLL boundary: symbols
  resolved from the system, from ksnet, from the vendored Lua static library
  or from a consumer's own translation units are referenced but not defined by
  ksengine.lib, so they drop out.

.EXAMPLE
  powershell -File tools/ksengine_export_surface.ps1 -Build build_qtfree -Out surface.csv
#>
param(
    [string]$Build = "build_qtfree",
    [string]$Out = "ksengine_export_surface.csv",
    [string]$Dumpbin = ""
)

$ErrorActionPreference = "Stop"

if (-not $Dumpbin) {
    $vc = Get-ChildItem "C:\Program Files\Microsoft Visual Studio" -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $vc) { throw "dumpbin.exe not found" }
    $Dumpbin = $vc.FullName
}
"dumpbin: $Dumpbin"

# --- 1. symbols ksengine.lib defines -------------------------------------
$lib = Get-ChildItem -Path $Build -Recurse -Filter "ksengine.lib" |
    Where-Object { $_.FullName -match '\\src\\engine\\' } | Select-Object -First 1
if (-not $lib) { throw "ksengine.lib not found under $Build/src/engine" }
"lib:     $($lib.FullName)"

$defined = New-Object 'System.Collections.Generic.HashSet[string]'
$undefRx = [regex]'^\s*[0-9A-F]+\s+[0-9A-F]+\s+(SECT[0-9A-F]+|UNDEF)\s+.*\bExternal\s+\|\s+(\S+)'

$line = 0
foreach ($l in & $Dumpbin /nologo /symbols $lib.FullName) {
    $line++
    if ($line % 200000 -eq 0) { "  ...$line lib lines" }
    $m = $undefRx.Match([string]$l)
    if (-not $m.Success) { continue }
    if ($m.Groups[1].Value -eq 'UNDEF') { continue }   # the engine's own imports
    [void]$defined.Add($m.Groups[2].Value)
}
"defined in ksengine.lib: $($defined.Count)"

# --- 2. symbols the consumers reference ----------------------------------
# Grouped per target: a symbol only has to come from ksengine.dll when some
# target references it *and* none of that target's own object files defines
# it. Without this, entities a consumer also compiles in (e.g. the simulator
# and the engine both carry a ks::sim::NativeRenderer) look required when
# they are not.
$objs = Get-ChildItem -Path $Build -Recurse -Filter "*.obj" -File |
    Where-Object { $_.FullName -notmatch '\\src\\engine\\' }
"consumer object files:   $($objs.Count)"

$definedBy = @{}   # target -> HashSet of symbols it defines
$undefBy   = @{}   # target -> HashSet of symbols it references

foreach ($o in $objs) {
    $parent = $o.Directory.Parent.Name          # e.g. "SimulatorApp.dir"
    $target = if ($parent -like '*.dir') { $parent -replace '\.dir$', '' }
              elseif ($o.Directory.Name -like '*.dir') { $o.Directory.Name -replace '\.dir$', '' }
              else { $parent }
    if (-not $definedBy.ContainsKey($target)) {
        $definedBy[$target] = New-Object 'System.Collections.Generic.HashSet[string]'
        $undefBy[$target]   = New-Object 'System.Collections.Generic.HashSet[string]'
    }
    $d = $definedBy[$target]; $u = $undefBy[$target]
    foreach ($l in & $Dumpbin /nologo /symbols $o.FullName) {
        $m = $undefRx.Match([string]$l)
        if (-not $m.Success) { continue }
        if ($m.Groups[1].Value -eq 'UNDEF') { [void]$u.Add($m.Groups[2].Value) }
        else                                { [void]$d.Add($m.Groups[2].Value) }
    }
}
"consumer targets:        $($definedBy.Count)"

$required = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($t in $definedBy.Keys) {
    foreach ($s in $undefBy[$t]) {
        if ($definedBy[$t].Contains($s)) { continue }   # resolved inside the target
        [void]$required.Add($s)
    }
}
"referenced but not self-defined: $($required.Count)"

# --- 3. keep only what ksengine.lib can supply ---------------------------
[void]$required.IntersectWith($defined)
"required from ksengine.dll: $($required.Count)"

# --- 4. mangled name -> C++ entity --------------------------------------
function Get-Entity([string]$m) {
    if ($m -notlike '?*') { return $null }
    $end = $m.IndexOf('@@')
    if ($end -lt 0) { $end = $m.Length }
    $prefix = $m.Substring(1, $end - 1)
    $parts = @($prefix -split '@')
    if ($parts.Count -eq 0) { return $null }
    $head = $parts[0]
    $kind = 'function'
    if ($head.StartsWith('?')) {
        $kind = 'special'
        $rest = $head.Substring(1) -replace '^(\d|_[A-Z0-9])+', '' -replace '^\?(AV|AU)', ''
        if ($rest -eq '' -and $parts.Count -gt 1) { $rest = $parts[1] }
        $head = $rest
    }
    $enclosing = @($parts | Select-Object -Skip 1)
    if ($enclosing.Count -gt 1) { [array]::Reverse($enclosing) }
    $chain = @($enclosing | Where-Object { $_ })
    return [pscustomobject]@{
        Name     = $head
        Scope    = ($chain -join '::')
        Kind     = $kind
        Template = ($head -like '$*') -or ($chain -contains '$')
        Mangled  = $m
    }
}

$entities = @{}
foreach ($m in $required) {
    $e = Get-Entity $m
    if ($null -eq $e) { continue }
    $key = "$($e.Scope)|$($e.Name)"
    if (-not $entities.ContainsKey($key)) {
        $entities[$key] = [pscustomobject]@{
            Scope    = $e.Scope
            Name     = $e.Name
            Kind     = $e.Kind
            Template = $e.Template
            Symbols  = 1
        }
    } else { $entities[$key].Symbols++ }
}

$rows = $entities.Values | Sort-Object Template, Scope, Name
"distinct entities to annotate: $($rows.Count)" |
    Write-Host -ForegroundColor Cyan
$rows | Export-Csv -NoTypeInformation -Path $Out -Encoding UTF8
"wrote $Out" | Write-Host

$rows | Group-Object Kind | Sort-Object Count -Descending |
    ForEach-Object { "  {0,-10} {1}" -f $_.Name, $_.Count }
$rows | Where-Object { $_.Template } | Select-Object -First 10 Scope, Name
