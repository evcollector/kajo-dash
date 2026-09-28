param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $SimulatorArgs
)

$ErrorActionPreference = 'Stop'

# ValueFromRemainingArguments is `$null` when a .bat launch supplies no
# options. Normalize it before prepending the default persistent profile;
# otherwise PowerShell splats a trailing empty native-process argument.
if ($null -eq $SimulatorArgs) {
    $SimulatorArgs = @()
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $projectRoot 'tools\lvgl_native_preview'
$buildDir = Join-Path $sourceDir 'build_simulator'
$lvglDir = Join-Path $projectRoot '.pio\libdeps\kajo\lvgl'
$stateFile = Join-Path $projectRoot 'build\native-simulator\preferences.bin'
$executable = Join-Path $buildDir 'Release\cyd_simulator.exe'

if (-not (Test-Path -LiteralPath (Join-Path $lvglDir 'lvgl.h'))) {
    throw "PlatformIO's LVGL dependency is missing. Run 'pio run -e kajo' once, then retry."
}

# MSBuild cannot replace the simulator executable while another instance from
# this build tree is running. Close only that exact executable; another project
# with a process named cyd_simulator is outside this launcher's scope.
$expectedExecutable = [System.IO.Path]::GetFullPath($executable)
$runningInstances = Get-Process -Name 'cyd_simulator' -ErrorAction SilentlyContinue | Where-Object {
    try {
        [System.StringComparer]::OrdinalIgnoreCase.Equals($_.Path, $expectedExecutable)
    } catch {
        $false
    }
}

foreach ($instance in $runningInstances) {
    Write-Host "Closing existing KAJO-Dash simulator process $($instance.Id)..."
    try {
        if ($instance.MainWindowHandle -ne 0) {
            $null = $instance.CloseMainWindow()
        }
        if (-not $instance.WaitForExit(3000)) {
            Write-Host "Existing simulator did not close cleanly; stopping process $($instance.Id)."
            Stop-Process -Id $instance.Id -Force
            $null = $instance.WaitForExit(3000)
        }
    } catch [System.InvalidOperationException] {
        # It exited between process discovery and the shutdown request.
    }
}

$cmakeCache = Join-Path $buildDir 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cmakeCache)) {
    cmake -Wno-dev -S $sourceDir -B $buildDir -A x64 `
        "-DLVGL_DIR=$lvglDir" `
        '-DCYD_BUILD_SIMULATOR=ON'
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

cmake --build $buildDir --config Release --target cyd_simulator
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not (Test-Path -LiteralPath $executable)) {
    throw "Native simulator executable was not produced: $executable"
}

$hasStateFile = $SimulatorArgs | Where-Object { $_ -like '--state-file=*' } | Select-Object -First 1
$hasStateFileArgument = $SimulatorArgs -contains '--state-file'
$isHeadless = $SimulatorArgs -contains '--headless'
if (-not $hasStateFile -and -not $hasStateFileArgument -and -not $isHeadless) {
    $SimulatorArgs = @("--state-file=$stateFile") + $SimulatorArgs
}

& $executable @SimulatorArgs
exit $LASTEXITCODE
