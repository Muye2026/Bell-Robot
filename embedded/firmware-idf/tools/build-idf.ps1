param(
  [string]$BuildDir = "build",
  [string]$Port = "",
  [switch]$ForceSetTarget,
  [switch]$Flash,
  [switch]$Monitor
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptDir "use-idf-env.ps1")

$projectDir = Split-Path -Parent $scriptDir
$idfBat = Join-Path $env:IDF_PATH "export.bat"
if (-not (Test-Path $idfBat)) {
  throw "export.bat not found: $idfBat"
}

$cmdParts = @(
  "call `"$idfBat`"",
  "cd /d `"$projectDir`""
)

$sdkconfigPath = Join-Path $projectDir "sdkconfig"
if ($ForceSetTarget -or -not (Test-Path $sdkconfigPath)) {
  $cmdParts += "idf.py -B `"$BuildDir`" set-target esp32s3"
}

$cmdParts += "idf.py -B `"$BuildDir`" build"

if ($Flash) {
  if ($Port) {
    $cmdParts += "idf.py -B `"$BuildDir`" -p $Port flash"
  } else {
    $cmdParts += "idf.py -B `"$BuildDir`" flash"
  }
}

if ($Monitor) {
  if ($Port) {
    $cmdParts += "idf.py -B `"$BuildDir`" -p $Port monitor"
  } else {
    $cmdParts += "idf.py -B `"$BuildDir`" monitor"
  }
}

$command = $cmdParts -join " && "
cmd /c $command
