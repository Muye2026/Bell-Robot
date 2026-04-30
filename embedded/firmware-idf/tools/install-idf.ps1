$espIdfRoot = "D:\Espressif\esp-idf-v5.4.4"
$pythonRoot = "C:\Users\23171\AppData\Local\Programs\Python\Python312"
$toolsRoot = "D:\Espressif\.espressif"
$logPath = "D:\Espressif\install-esp-idf-v5.4.4.log"

if (-not (Test-Path $espIdfRoot)) {
  throw "ESP-IDF not found: $espIdfRoot"
}
if (-not (Test-Path (Join-Path $pythonRoot "python.exe"))) {
  throw "Python not found: $pythonRoot"
}

$cmd = "set IDF_TOOLS_PATH=$toolsRoot&& set PATH=$pythonRoot;$pythonRoot\Scripts;%PATH%&& call `"$espIdfRoot\install.bat`" esp32s3 > `"$logPath`" 2>&1"
cmd /c $cmd
