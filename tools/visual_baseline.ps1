# Visual baseline: one capture run per scenario, compared against the signature
# kept in tests/visual. The app writes both the PNG (for a human) and the digest
# (for the test) from the same frame, through hooks that already existed:
#   KS_SCREENSHOT / KS_SCREENSHOT_DELAY   save a frame N frames in and exit
#                                         (the values below carry an 'f'
#                                          suffix; a bare number is ms)
#   KS_VISUAL_DIGEST                      where the frame's signature goes
#   KS_AUTOSTART / KS_CAM                 drive session, no menu, chosen view
#
#   pwsh -File tools/visual_baseline.ps1                      # check the showroom shot
#   pwsh -File tools/visual_baseline.ps1 -WithDriving          # + the driving shots
#   pwsh -File tools/visual_baseline.ps1 -Record -WithDriving  # accept today's frames
#   pwsh -File tools/visual_baseline.ps1 -LocationMaterials   # the track's location-preset
#                                                             # off/on pair, one pass
#
# The location-materials pair captures the same track frame twice in one pass,
# with KS_LOCATION_MATERIALS=0 and =1, and prints both grid distances: the off
# run against the stored baseline (drift), and the on run against the off run of
# the same pass (what the presets did). The second is reported, not required: a
# feature whose point is changing surfaces must change the frame, and a distance
# of 0.0000 is the interesting result - it says the affected surfaces are too
# small in this shot for the 12x10 grid to see them. The off capture is the
# stored reference, recorded with -Record like any other scenario.
#
# A check passes on the 12x10 luma grid within tolerance, not on the hash: see
# FrameDigest.h. The hash is printed anyway, so a difference is easy to spot.
param(
    [string]$Exe = '',
    [string]$OutDir = '',
    [switch]$Record,
    [switch]$WithDriving,
    [switch]$LocationMaterials,
    [string]$Only = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ([string]::IsNullOrEmpty($Exe)) {
    $Exe = Join-Path $root 'build\bin\Release\ksim.exe'
}
if ([string]::IsNullOrEmpty($OutDir)) {
    $OutDir = Join-Path $root 'tests\visual'
}
$checker = Join-Path $root 'build\bin\Release\visual_baseline_test.exe'
if (-not (Test-Path $Exe)) { Write-Output "no app at $Exe"; exit 2 }
if (-not (Test-Path $checker)) { Write-Output "no checker at $checker"; exit 2 }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# The three things this turn's work added, each in the view where it shows:
# the tracked wheels' blob shadows and the grass field are behind the car in
# chase, and the cockpit view is where the field rushes past the nose.
#  - 1  + 1  + 3  + 1  + 1  + 3  + 1  + 1  + 3  + 1  + 1  + 3  + 1
# The showroom shot runs anywhere: it proves the whole chain (session, frame,
# readback, digest) and catches a rendering regression in the menu path.
$scenarios = @(
    @{ Name = 'showroom_chase'; Env = @{ KS_AUTOSTART = '1'; KS_CAM = 'chase'; KS_START_IN_GARAGE = '1'; KS_PAUSE_ON_FOCUS_LOSS = '0'; KS_SCREENSHOT_DELAY = '180f' } }
)
# The driving shots are what this baseline exists for - blob shadows under the
# wheels and the grass field with its fading edge - but they need a track and a
# car installed, so they are opt-in instead of failing on a bare machine.
if ($WithDriving) {
    $scenarios += @(
        @{ Name = 'track_chase';    Env = @{ KS_AUTOSTART = '1'; KS_CAM = 'chase';   KS_START_IN_GARAGE = '0'; KS_PAUSE_ON_FOCUS_LOSS = '0'; KS_HIDE_MENU = '1'; KS_SCREENSHOT_DELAY = '240f' } },
        @{ Name = 'track_cockpit';  Env = @{ KS_AUTOSTART = '1'; KS_CAM = 'cockpit'; KS_START_IN_GARAGE = '0'; KS_PAUSE_ON_FOCUS_LOSS = '0'; KS_HIDE_MENU = '1'; KS_SCREENSHOT_DELAY = '240f' } },
        @{ Name = 'track_late';     Env = @{ KS_AUTOSTART = '1'; KS_CAM = 'chase';   KS_START_IN_GARAGE = '0'; KS_PAUSE_ON_FOCUS_LOSS = '0'; KS_HIDE_MENU = '1'; KS_SCREENSHOT_DELAY = '600f' } }
    )
}

$failures = 0
$ran = 0

# One capture: the app saves the frame and the digest, and this returns the
# digest path (or $null, having counted the failure). Shared by the scenario loop
# and the off/on pair so both fail the same way.
function Invoke-Capture {
    param([string]$Name, [hashtable]$Env)
    $png = Join-Path $OutDir "$Name.png"
    $sig = Join-Path $OutDir "$Name.sig"

    $env:KS_SCREENSHOT = $png
    $env:KS_VISUAL_DIGEST = $sig
    foreach ($key in $Env.Keys) {
        Set-Item -Path "env:$key" -Value $Env[$key]
    }

    $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe) -PassThru
    if (-not $proc.WaitForExit(120000)) {
        $proc.Kill()
        Write-Output "$Name : TIMEOUT"
        $script:failures++
        return $null
    }
    foreach ($key in $Env.Keys) { Remove-Item -Path "env:$key" -ErrorAction SilentlyContinue }
    if (-not (Test-Path $sig)) {
        Write-Output "$Name : no digest (exit $($proc.ExitCode)) - see the run output"
        $script:failures++
        return $null
    }
    $script:ran++
    return $sig
}

foreach ($scenario in $scenarios) {
    $name = $scenario.Name
    if (-not [string]::IsNullOrEmpty($Only) -and $name -ne $Only) { continue }

    $sig = Invoke-Capture -Name $name -Env $scenario.Env
    if ($null -eq $sig) { continue }
    $ref = Join-Path $OutDir "$name.baseline"
    $mode = if ($Record) { '--record' } else { '--check' }
    & $checker $mode $sig $ref
    if ($LASTEXITCODE -ne 0) { $failures++ }
    # Named regions: when the scenario carries a <name>.regions file, the
    # capture PNG is also compared against the recorded <name>.golden.png,
    # region by region (see src/simulator/VisualRegions.h). -Record stores
    # today's capture as the golden; a missing golden says so, not a failure.
    $regionsFile = Join-Path $OutDir "$name.regions"
    $regionChecker = Join-Path $root 'build\bin\Release\visual_regions_test.exe'
    if ((Test-Path $regionsFile) -and (Test-Path $regionChecker)) {
        $png = Join-Path $OutDir "$name.png"
        $goldenPng = Join-Path $OutDir "$name.golden.png"
        if ($Record) {
            Copy-Item -Path $png -Destination $goldenPng -Force
            Write-Output "$name : regions golden recorded"
        } elseif (Test-Path $goldenPng) {
            & $regionChecker --check $png $goldenPng $regionsFile
            if ($LASTEXITCODE -ne 0) { $failures++ }
        } else {
            Write-Output "$name : no regions golden at $goldenPng; run -Record"
        }
    }
}

# The location-preset pair: the same track frame with the feature off and on,
# captured in this one pass so the comparison needs no manual runs.
if ($LocationMaterials -and ([string]::IsNullOrEmpty($Only) -or $Only -eq 'track_location')) {
    $common = @{
        KS_AUTOSTART = '1';
        KS_CAM = 'chase';
        KS_START_IN_GARAGE = '0';
        KS_PAUSE_ON_FOCUS_LOSS = '0';
        KS_HIDE_MENU = '1';
        KS_SCREENSHOT_DELAY = '240f'
    }
    $offEnv = $common.Clone()
    $offEnv['KS_LOCATION_MATERIALS'] = '0'
    $onEnv = $common.Clone()
    $onEnv['KS_LOCATION_MATERIALS'] = '1'

    $offSig = Invoke-Capture -Name 'track_location_off' -Env $offEnv
    $onSig = Invoke-Capture -Name 'track_location_on' -Env $onEnv
    $ref = Join-Path $OutDir 'track_location.baseline'
    if ($null -ne $offSig -and $null -ne $onSig) {
        if ($Record) {
            & $checker --record $offSig $ref | Out-Null
            Write-Output 'track_location : baseline recorded from the off run'
        } elseif (Test-Path $ref) {
            Write-Output 'track_location : off run against the stored baseline'
            & $checker --check $offSig $ref
            if ($LASTEXITCODE -ne 0) { $failures++ }
        } else {
            Write-Output 'track_location : no stored baseline; the off run of this pass is the reference'
        }
        # What the presets did, measured against the same frame with them off.
        Write-Output 'track_location : on run against the off run of this pass'
        & $checker --check $onSig $offSig
        $distance = $LASTEXITCODE
        if ($distance -ne 0) {
            Write-Output 'track_location : the presets moved the frame beyond the grid tolerance'
        } else {
            Write-Output 'track_location : the grid cannot see the presets in this shot (that is a result, not a pass)'
        }
    }
}

Remove-Item Env:KS_SCREENSHOT -ErrorAction SilentlyContinue
Remove-Item Env:KS_VISUAL_DIGEST -ErrorAction SilentlyContinue

if ($ran -eq 0) { Write-Output 'visual baseline: no scenario ran'; exit 2 }
if ($failures -gt 0) { Write-Output "visual baseline: $failures failed"; exit 1 }
Write-Output "visual baseline: $ran ok"
exit 0
