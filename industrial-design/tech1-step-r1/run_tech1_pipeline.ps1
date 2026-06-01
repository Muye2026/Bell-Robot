param(
    [string]$ReferenceImage = "D:\Project\Bell-Robot\industrial-design\concept-r3\01-tech1-angular-a.png"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$freeCadCmd = if ($env:FREECAD_CMD) {
    $env:FREECAD_CMD
} else {
    "D:\Program Files\FreeCAD\FreeCAD_1.1.1-Windows-x86_64-py311\FreeCADCmd.exe"
}
$bundledPython = "C:\Users\23171\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"

if (!(Test-Path -LiteralPath $freeCadCmd)) {
    throw "FreeCADCmd not found: $freeCadCmd"
}
if (!(Test-Path -LiteralPath $bundledPython)) {
    throw "Bundled Python not found: $bundledPython"
}
if (!(Test-Path -LiteralPath $ReferenceImage)) {
    throw "Reference image not found: $ReferenceImage"
}

function Invoke-FreeCADScript {
    param([string]$Path)
    $command = "p=r'$Path'; exec(compile(open(p, encoding='utf-8').read(), p, 'exec'), {'__file__': p, '__name__': '__main__'})"
    & $freeCadCmd -c $command
    if ($LASTEXITCODE -ne 0) {
        throw "FreeCAD script failed: $Path"
    }
}

Write-Host "[1/4] Generate model"
Invoke-FreeCADScript (Join-Path $scriptDir "generate_tech1_step.py")

Write-Host "[2/4] Validate geometry"
Invoke-FreeCADScript (Join-Path $scriptDir "validate_tech1_step.py")

Write-Host "[3/4] Render review views"
Invoke-FreeCADScript (Join-Path $scriptDir "render_tech1_views_vtk.py")

Write-Host "[4/4] Compose comparison sheets"
$env:TECH1_REFERENCE_IMAGE = $ReferenceImage
& $bundledPython (Join-Path $scriptDir "compose_tech1_review_sheet.py")
if ($LASTEXITCODE -ne 0) {
    throw "Compose review sheet failed"
}

Write-Host "Pipeline complete."
Write-Host "Reference image: $ReferenceImage"
Write-Host "Outputs:"
Write-Host "  D:\Project\Bell-Robot\_cad-output"
Write-Host "  D:\Project\Bell-Robot\_render-raster"
