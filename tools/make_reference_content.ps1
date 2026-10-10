# Roadmap 2.4 — synthetic reference content generator.
#
# Produces the committed reference bakes the renderer test and the windowed
# app both consume:
#
#   content/baked/               track-like scene: textured "sign" quad,
#                                "smooth"/"rough" roughness pair, "coated"
#                                clear-coat quad (brief P3), "metal"
#                                quad (brief P1: metalness cell is a map,
#                                textures/metal_mask.dds), "lodable"
#                                quad with a [0..1] m NMS2 window, "plain"
#                                fallback quad (no materials.txt row),
#                                "asphalt"/"asphalt_tilt" normal-map pair
#                                (brief P5, cool-grey track surface), "disc"
#                                brake-disc quad (brief P6: its manifest
#                                name is the one that trips the renderer's
#                                disc heuristic), and a 40x40 m textured
#                                ground plane;
#   content/cars/refcar/baked/   reference car: a single textured body box
#                                with an authored [0..1000] m window.
#
# Formats mirror the runtime readers byte for byte:
#   - .nmsh  = NMS2 magic, u32 vcount, u32 icount, f32 lodIn, f32 lodOut,
#              NativeVertex[] (12 floats: pos, normal, uv, color), u32[] —
#              see NativeRenderer::loadMeshFromFile;
#   - DDS    = the same 4x4 uncompressed RGBA header tests/ksengine's
#              writeTextureFixture() uses (proven with the shared DdsReader),
#              parameterised by colour;
#   - materials.txt = "mesh \t albedo \t roughness \t metalness" rows — the
#              raw KN5 texture name goes in albedo (the runtime sanitises it
#              into textures/, MaterialCache.h); roughness/metalness are
#              dual-typed (brief P1): a number is the scalar, a dotted name
#              a map texture. "plain" intentionally has no row so the
#              fallback path stays covered. Brief P3 appends an optional
#              6th clear-coat cell (only "coated" uses it here).
#
# Idempotent: wipes the two output trees before writing. BinaryWriter is
# little-endian on every platform PowerShell runs on (ECMA-334), which is
# exactly what the C++ readers assume.
#
# Usage:  powershell -File tools/make_reference_content.ps1

param(
    [string]$ContentRoot = (Join-Path $PSScriptRoot "..\content")
)

$ErrorActionPreference = "Stop"

# --- primitives ------------------------------------------------------------

function Write-Ascii([System.IO.BinaryWriter]$w, [string]$s) {
    $w.Write([System.Text.Encoding]::ASCII.GetBytes($s))
}

# Appends one face to $Faces: four corners p, p+e1, p+e1+e2, p+e2 (so
# e1 x e2 is the outward normal), the face normal and per-corner UVs.
# All points/edges are 3-element arrays.
function Add-Face(
    [System.Collections.ArrayList]$Faces,
    [array]$P, [array]$E1, [array]$E2, [array]$Nrm) {
    $p0 = @([float]$P[0],          [float]$P[1],          [float]$P[2])
    $p1 = @([float]($P[0]+$E1[0]), [float]($P[1]+$E1[1]), [float]($P[2]+$E1[2]))
    $p3 = @([float]($P[0]+$E2[0]), [float]($P[1]+$E2[1]), [float]($P[2]+$E2[2]))
    $p2 = @([float]($P[0]+$E1[0]+$E2[0]),
            [float]($P[1]+$E1[1]+$E2[1]),
            [float]($P[2]+$E1[2]+$E2[2]))
    [void]$Faces.Add(@{
        Pos = @($p0, $p1, $p2, $p3)
        Nrm = @([float]$Nrm[0], [float]$Nrm[1], [float]$Nrm[2])
        UV  = @(@(0.0, 0.0), @(0.0, 1.0), @(1.0, 1.0), @(1.0, 0.0))
    })
}

# One NMS2 mesh: magic, counts, authored LOD window, vertices, indices.
function Write-Nms2([string]$Path, [System.Collections.ArrayList]$Faces,
                    [float]$LodIn, [float]$LodOut) {
    $dir = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $w = [System.IO.BinaryWriter]::new([System.IO.File]::Create($Path))
    try {
        Write-Ascii $w "NMS2"
        $w.Write([uint32]($Faces.Count * 4))   # vertices
        $w.Write([uint32]($Faces.Count * 6))   # indices
        $w.Write([float]$LodIn)
        $w.Write([float]$LodOut)
        foreach ($f in $Faces) {
            foreach ($i in 0..3) {
                $p = $f.Pos[$i]; $uv = $f.UV[$i]; $n = $f.Nrm
                $w.Write([float]$p[0]); $w.Write([float]$p[1]); $w.Write([float]$p[2])
                $w.Write([float]$n[0]); $w.Write([float]$n[1]); $w.Write([float]$n[2])
                $w.Write([float]$uv[0]); $w.Write([float]$uv[1])
                $w.Write([float]1.0); $w.Write([float]1.0)
                $w.Write([float]1.0); $w.Write([float]1.0)
            }
        }
        $base = [uint32]0
        foreach ($f in $Faces) {
            foreach ($i in @(0, 1, 2, 0, 2, 3)) { $w.Write([uint32]($base + $i)) }
            $base += 4
        }
    }
    finally {
        $w.Flush(); $w.Dispose()
    }
}

# 4x4 uncompressed RGBA DDS — byte layout mirrors writeTextureFixture() in
# tests/ksengine/test_renderer.cpp (proven with the shared DdsReader),
# parameterised by colour.
function Write-Dds([string]$Path, [int]$R, [int]$G, [int]$B) {
    $dir = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $w = [System.IO.BinaryWriter]::new([System.IO.File]::Create($Path))
    try {
        Write-Ascii $w "DDS "
        $w.Write([uint32]124)               # header size
        $w.Write([uint32]0x0000100F)         # caps|height|width|pitch|pf
        $w.Write([uint32]4)                  # height
        $w.Write([uint32]4)                  # width
        $w.Write([uint32]16)                 # pitch
        $w.Write([uint32]0)                  # depth
        $w.Write([uint32]0)                  # mip count (as writeTextureFixture)
        foreach ($i in 1..11) { $w.Write([uint32]0) }   # reserved
        $w.Write([uint32]32)                 # pixel format size
        $w.Write([uint32]0x42)               # DDPF_RGB | DDPF_ALPHA
        $w.Write([uint32]0)                  # fourcc
        $w.Write([uint32]32)                 # bits per pixel
        $w.Write([uint32]0x000000FF)         # R mask
        $w.Write([uint32]0x0000FF00)         # G mask
        $w.Write([uint32]0x00FF0000)         # B mask
        $w.Write([uint32]4278190080)         # A mask (0xFF000000; hex literal would be a negative int32)
        foreach ($i in 1..5) { $w.Write([uint32]0) }    # caps/reserved
        foreach ($i in 1..16) {
            $w.Write([byte]$R); $w.Write([byte]$G); $w.Write([byte]$B); $w.Write([byte]255)
        }
    }
    finally {
        $w.Flush(); $w.Dispose()
    }
}

function Write-Lines([string]$Path, [string[]]$Lines) {
    $dir = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    [System.IO.File]::WriteAllText($Path, (($Lines -join "`r`n") + "`r`n"))
}

# Vertical quad facing -z (both the test camera and the windowed chase
# camera sit at -z), centred on (0, $Cy) so the windowed shot sees it above
# the car placeholder box. Stagger $Z to pin the depth order.
function Add-QuadMinusZ([System.Collections.ArrayList]$Faces, [float]$Z, [float]$Cy) {
    Add-Face $Faces @(-1.0, ($Cy - 1.0), $Z) @(0.0, 2.0, 0.0) @(2.0, 0.0, 0.0) @(0.0, 0.0, -1.0)
}

# --- track scene ------------------------------------------------------------

$track = Join-Path $ContentRoot "baked"
if (Test-Path $track) { Remove-Item -Recurse -Force $track }

Write-Lines (Join-Path $track "manifest.txt") @(
    "plain", "sign", "smooth", "rough", "coated", "metal", "lodable",
    "asphalt", "asphalt_tilt", "disc", "ground"
)

# Tabs are the format (MaterialCache.h). "plain" has NO row on purpose:
# defaults + white albedo fallback must stay covered. "skin:paint.dds" is
# written raw on purpose: the colon is invalid on disk, so the runtime has
# to sanitise it to textures/skin_paint.dds exactly like the baker did.
# "metal" (brief P1) shares smooth's geometry and roughness 0.05 but its
# metalness cell is a *texture* name: metal_mask.dds is solid red, i.e.
# mask.r = 1.0 -> a fully metallic dielectric-turned-metal surface whose
# GBuffer RT0.a must reach deferred_lighting.frag's F0.
# "coated" (brief P3) is "rough" with ONE difference: the optional 6th
# cell clears the clear-coat flag to 1 (normal cell 5 left empty), so the
# test A/B isolates the coat lobe against an identical broad base. Every
# other row stays the legacy width it had before P3.
# "asphalt"/"asphalt_tilt" (brief P5) are the first reference rows to use
# cell 5 (normal tex): cool-grey asphalt at roughness 0.70 with a flat
# normal, and the same material with a tangent-space +x texel (255,128,
# 128) whose N sits perpendicular to the test sun — the pair pins the
# normal-map chain down to NdotL.
# "disc" (brief P6) is a dark-grey fully metallic brake-disc quad at a
# broad roughness 0.70: the base stays dim (no diffuse lobe on metal) so
# the heat glow has headroom, and its manifest NAME is what flags it as a
# disc in NativeRenderer::setMesh — the row itself is a plain legacy 4-cell
# row, no format change.
Write-Lines (Join-Path $track "materials.txt") @(
    "# reference bake (roadmap 2.4/2.5, brief P1/P3/P5/P6) - regenerate with tools/make_reference_content.ps1",
    "sign`tskin:paint.dds`t0.35`t0.00",
    "smooth`tskin:paint.dds`t0.05`t0.00",
    "rough`tskin:paint.dds`t0.95`t0.00",
    "coated`tskin:paint.dds`t0.95`t0.00`t`t1",
    "metal`tskin:paint.dds`t0.05`tmetal_mask.dds",
    "lodable`tskin:paint.dds`t0.35`t0.00",
    "asphalt`tasphalt.dds`t0.70`t0.00`tasphalt_n.dds",
    "asphalt_tilt`tasphalt.dds`t0.70`t0.00`ttilt_n.dds",
    "disc`tdisc.dds`t0.70`t1.00",
    "ground`tskin:paint.dds`t0.60`t0.00"
)

Write-Dds (Join-Path $track "textures/skin_paint.dds") 220 80 30
Write-Dds (Join-Path $track "textures/metal_mask.dds") 255 0 0
Write-Dds (Join-Path $track "textures/asphalt.dds") 86 88 92
Write-Dds (Join-Path $track "textures/asphalt_n.dds") 128 128 255   # flat +z
Write-Dds (Join-Path $track "textures/tilt_n.dds") 255 128 128      # +x: N _|_ sun
Write-Dds (Join-Path $track "textures/disc.dds") 60 60 64           # dark brake-disc metal

# Quads sit at y = 2..4 (centre 3) with staggered z; the test aims its own
# camera at (0, 3, 0). LOD windows: only "lodable" carries one ([0..1] m),
# everything else the NMS2 default (no window).
$noWindow = [float]1e9
foreach ($q in @(
    @{ File = "plain.nmsh";   Z =  0.00 },
    @{ File = "sign.nmsh";    Z = -0.02 },
    @{ File = "smooth.nmsh";  Z = -0.04 },
    @{ File = "rough.nmsh";   Z = -0.06 },
    @{ File = "coated.nmsh";  Z = -0.12 },
    @{ File = "metal.nmsh";   Z = -0.10 },
    @{ File = "lodable.nmsh"; Z = -0.08 },
    @{ File = "asphalt.nmsh";      Z = -0.14 },
    @{ File = "asphalt_tilt.nmsh"; Z = -0.16 },
    @{ File = "disc.nmsh";         Z = -0.18 }   # brief P6: brake-disc glow quad
)) {
    $faces = [System.Collections.ArrayList]::new()
    Add-QuadMinusZ $faces ([float]$q.Z) 3.0
    if ($q.File -eq "lodable.nmsh") {
        Write-Nms2 (Join-Path $track $q.File) $faces 0.0 1.0
    } else {
        Write-Nms2 (Join-Path $track $q.File) $faces 0.0 $noWindow
    }
}

# 40x40 m ground plane at y=0, normal +y (e1 x e2 = +y).
$ground = [System.Collections.ArrayList]::new()
Add-Face $ground @(-20.0, 0.0, -20.0) @(0.0, 0.0, 40.0) @(40.0, 0.0, 0.0) @(0.0, 1.0, 0.0)
Write-Nms2 (Join-Path $track "ground.nmsh") $ground 0.0 $noWindow

# --- reference car ----------------------------------------------------------

$carRoot = Join-Path $ContentRoot "cars/refcar"
if (Test-Path $carRoot) { Remove-Item -Recurse -Force $carRoot }
$car = Join-Path $carRoot "baked"

Write-Lines (Join-Path $car "manifest.txt") @("body")
Write-Lines (Join-Path $car "materials.txt") @(
    "# reference car bake (roadmap 2.4) - regenerate with tools/make_reference_content.ps1",
    "body`tcar_paint.dds`t0.30`t0.00"
)
Write-Dds (Join-Path $car "textures/car_paint.dds") 60 170 90

# 4.4 x 1.4 x 1.8 m body box (same footprint as the runtime placeholder),
# six outward-facing faces, centred on the origin so the car_ entity
# transform (0, 0.35, 0) drops it on the ground.
$x0 = -0.9; $x1 = 0.9; $y0 = -0.4; $y1 = 1.0; $z0 = -2.2; $z1 = 2.2
$body = [System.Collections.ArrayList]::new()
Add-Face $body @($x1, $y0, $z1) @(0.0, 0.0, ($z0-$z1)) @(0.0, ($y1-$y0), 0.0) @(1.0, 0.0, 0.0)
Add-Face $body @($x0, $y0, $z0) @(0.0, 0.0, ($z1-$z0)) @(0.0, ($y1-$y0), 0.0) @(-1.0, 0.0, 0.0)
Add-Face $body @($x0, $y1, $z0) @(0.0, 0.0, ($z1-$z0)) @(($x1-$x0), 0.0, 0.0) @(0.0, 1.0, 0.0)
Add-Face $body @($x0, $y0, $z0) @(($x1-$x0), 0.0, 0.0) @(0.0, 0.0, ($z1-$z0)) @(0.0, -1.0, 0.0)
Add-Face $body @($x0, $y0, $z1) @(($x1-$x0), 0.0, 0.0) @(0.0, ($y1-$y0), 0.0) @(0.0, 0.0, 1.0)
Add-Face $body @($x0, $y0, $z0) @(0.0, ($y1-$y0), 0.0) @(($x1-$x0), 0.0, 0.0) @(0.0, 0.0, -1.0)
Write-Nms2 (Join-Path $car "body.nmsh") $body 0.0 1000.0

Write-Host "reference content written under $ContentRoot"
foreach ($root in @((Join-Path $ContentRoot "baked"), $car)) {
    Get-ChildItem -Recurse -File $root | ForEach-Object {
        "  {0,8}  {1}" -f $_.Length, $_.FullName.Substring((Resolve-Path $ContentRoot).Path.Length + 1)
    }
}
