param(
  [string]$Version = "6.0.5"
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sdkRoot = Join-Path $projectRoot ".tools\emsdk"
$launcher = Join-Path $sdkRoot "emsdk.bat"

if (-not (Test-Path -LiteralPath $launcher)) {
  New-Item -ItemType Directory -Force -Path (Split-Path $sdkRoot) | Out-Null
  git clone --depth 1 https://github.com/emscripten-core/emsdk.git $sdkRoot
  if ($LASTEXITCODE -ne 0) { throw "Could not clone the Emscripten SDK" }
}

Push-Location $sdkRoot
try {
  & $launcher install $Version
  if ($LASTEXITCODE -ne 0) { throw "Could not install Emscripten $Version" }
  & $launcher activate $Version
  if ($LASTEXITCODE -ne 0) { throw "Could not activate Emscripten $Version" }
} finally {
  Pop-Location
}

$compiler = Join-Path $sdkRoot "upstream\emscripten\emcc.exe"
if (-not (Test-Path -LiteralPath $compiler)) { throw "Emscripten compiler was not installed" }
& $compiler --version | Select-Object -First 1
Write-Output "Emscripten $Version is ready at $sdkRoot"
