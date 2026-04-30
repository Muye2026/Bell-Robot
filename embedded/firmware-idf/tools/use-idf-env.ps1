$espIdfRoot = "D:\Espressif\esp-idf-v5.4.4"
$pythonRoot = "C:\Users\23171\AppData\Local\Programs\Python\Python312"
$toolsRoot = "D:\Espressif\.espressif"

if (-not (Test-Path $espIdfRoot)) {
  throw "ESP-IDF not found: $espIdfRoot"
}

$env:IDF_PATH = $espIdfRoot
$env:IDF_TOOLS_PATH = $toolsRoot
$env:PATH = "$pythonRoot;$pythonRoot\Scripts;$env:PATH"

Write-Host "IDF_PATH=$env:IDF_PATH"
Write-Host "IDF_TOOLS_PATH=$env:IDF_TOOLS_PATH"
Write-Host "Python root added: $pythonRoot"
