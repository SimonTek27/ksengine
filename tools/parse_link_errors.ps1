<#
.SYNOPSIS
  Turns MSVC link errors (LNK2019 / LNK2001) into the list of engine
  entities that need a KSENGINE_API annotation.

.DESCRIPTION
  ksengine.dll only exports what is annotated with KSENGINE_API
  (src/engine/KsExport.h). Linking the executables therefore reports every
  engine entity they reach from outside the DLL as an unresolved external:

    SimulatorApp.obj : error LNK2019: unresolved external symbol
      "public: bool __cdecl ks::sim::SimulationLoop::initialize(void)"
      (?initialize@SimulationLoop@sim@ks@@QEAA_NXZ) referenced in function ...

  Both spellings are parsed; the mangled one is authoritative because it
  carries the enclosing scope. MSVC mangles as

      ?name@Innermost@Ns@Outer@@<signature>

  i.e. the components after the name run outermost-last, so reversing them
  gives the C++ qualified name.

.EXAMPLE
  powershell -File tools/parse_link_errors.ps1 -Log build.log
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$Log,

    [string]$Out = ""
)

if (-not (Test-Path $Log)) {
    Write-Error "log not found: $Log"
    exit 1
}

$text = Get-Content -Raw -Path $Log

# "unresolved external symbol "readable" (?mangled)" or "... __imp_?mangled"
$rx = [regex]'unresolved external symbol\s+(?:"[^"]*"\s+\((\?[^\)]+)\)|((?:__imp_)?\?[^\s]+))'

function Get-SymbolInfo([string]$mangled) {
    $m = $mangled -replace '^__imp_', ''
    $kind = if ($mangled -like '__imp_*') { 'import' } else { 'function' }

    if ($m -notlike '?*') {
        return [pscustomobject]@{ Name = $m; Scope = ''; Kind = 'other'; Template = $false }
    }

    $end = $m.IndexOf('@@')
    if ($end -lt 0) { $end = $m.Length }
    # Drop the single leading '?': special names keep a second one, so they
    # read as ?0Foo / ?_7Foo / ?_R0?AVFoo once the mangling prefix is gone.
    $prefix = $m.Substring(1, $end - 1)
    $parts = @($prefix -split '@')
    if ($parts.Count -eq 0) { return $null }

    $head = $parts[0]

    # Special names: constructors, destructors, vtables, RTTI, deleting dtors.
    #   ??0Foo@@        ctor of Foo        ??1Foo@@     dtor of Foo
    #   ??_7Foo@@       vftable of Foo     ??_R0?AVFoo@@  type descriptor
    if ($head.StartsWith('?')) {
        $kind = 'special'
        $rest = $head.Substring(1)
        $rest = $rest -replace '^(\d|_[A-Z0-9])+', ''
        $rest = $rest -replace '^\?(AV|AU)', ''
        if ($rest -eq '' -and $parts.Count -gt 1) {
            $rest = $parts[1]
            $parts = @($parts[0])
        }
        $head = $rest
    }

    # Components after the name list the enclosing scopes outermost-last.
    $enclosing = @($parts | Select-Object -Skip 1)
    if ($enclosing.Count -gt 1) { [array]::Reverse($enclosing) }
    $chain = @($enclosing | Where-Object { $_ })

    $isTemplate = ($head -like '$*') -or ($chain -contains '$')

    return [pscustomobject]@{
        Name     = $head
        Scope    = ($chain -join '::')
        Kind     = $kind
        Template = $isTemplate
        Mangled  = $m
    }
}

$seen = @{}
foreach ($m in $rx.Matches($text)) {
    $mangled = $m.Groups[1].Value
    if ($mangled -eq '') { $mangled = $m.Groups[2].Value }
    if ($mangled -eq '') { continue }

    $info = Get-SymbolInfo $mangled
    if ($null -eq $info) { continue }

    $key = "$($info.Scope)::$(($info.Name) -replace '^_+','')"
    if (-not $seen.ContainsKey($key)) {
        $seen[$key] = [pscustomobject]@{
            Scope    = $info.Scope
            Name     = $info.Name
            Kind     = $info.Kind
            Template = $info.Template
            Import   = ($mangled -like '__imp_*')
            Count    = 0
            Mangled  = $info.Mangled
        }
    }
    $seen[$key].Count++
    if ($mangled -like '__imp_*') { $seen[$key].Import = $true }
}

$result = $seen.Values | Sort-Object Scope, Name
"total: $($result.Count) distinct unresolved symbols" |
    Write-Host -ForegroundColor Cyan

$rows = $result |
    Select-Object @{n = 'Scope'; e = { $_.Scope } },
                  @{n = 'Name'; e = { $_.Name } },
                  @{n = 'Kind'; e = { $_.Kind } },
                  @{n = 'Template'; e = { $_.Template } },
                  @{n = 'Import'; e = { $_.Import } },
                  @{n = 'Count'; e = { $_.Count } }

if ($Out) {
    $rows | Export-Csv -NoTypeInformation -Path $Out -Encoding UTF8
    "wrote $Out" | Write-Host
} else {
    $rows | Format-Table -AutoSize
}
