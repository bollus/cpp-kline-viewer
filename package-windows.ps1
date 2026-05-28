param(
  [string]$BuildDir = "build-windows",
  [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

cmake -S . -B $BuildDir -DCMAKE_BUILD_TYPE=$Config
cmake --build $BuildDir --config $Config

$exe = Join-Path $BuildDir "$Config/q4j_kline_viewer.exe"
if (!(Test-Path $exe)) {
  $exe = Join-Path $BuildDir "q4j_kline_viewer.exe"
}
if (!(Test-Path $exe)) {
  throw "Cannot find q4j_kline_viewer.exe under $BuildDir"
}

$packageDir = Join-Path $BuildDir "package"
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
Copy-Item $exe $packageDir -Force

$windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
if (!$windeployqt) {
  throw "windeployqt was not found. Open a Qt Developer PowerShell or add Qt bin directory to PATH."
}

& $windeployqt.Source --release --compiler-runtime (Join-Path $packageDir "q4j_kline_viewer.exe")

$zip = Join-Path $BuildDir "q4j-kline-viewer-windows.zip"
if (Test-Path $zip) {
  Remove-Item $zip -Force
}
Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $zip

Write-Host "Windows package created: $zip"
