[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$armGcc = "D:\GCC\ARMGCC\10 2021.10\bin\arm-none-eabi-gcc.exe"
if (-not (Test-Path -LiteralPath $armGcc -PathType Leaf)) {
    throw "ARM GCC was not found at $armGcc"
}
$outputDirectory = Join-Path $workspaceRoot "build\arm-check"
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$includes = @(
    "APP\Algorithm\Mahony\include",
    "APP\Algorithm\PID\include",
    "Device\Sensor\IMU",
    "Device\Sensor\IMU\ICM45686",
    "Device\Sensor\IMU\BMI088",
    "Mode\Car\include"
)
$includeArguments = @()
foreach ($include in $includes) {
    $includeArguments += @("-I", (Join-Path $workspaceRoot $include))
}
$sources = @(
    "Mode\Car\Tracking\vision_track_mode.c",
    "Device\Sensor\IMU\ICM45686\device_icm45686.c",
    "Device\Sensor\IMU\BMI088\device_bmi088.c"
)

foreach ($source in $sources) {
    $sourcePath = Join-Path $workspaceRoot $source
    $objectName = ([System.IO.Path]::GetFileNameWithoutExtension($source) + ".o")
    $objectPath = Join-Path $outputDirectory $objectName
    & $armGcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror `
        -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -ffreestanding `
        -fstack-usage @includeArguments -c $sourcePath -o $objectPath
    if ($LASTEXITCODE -ne 0) {
        throw "ARM strict compile failed for $source"
    }
}

Write-Host "ARM strict compile tests: PASS"
