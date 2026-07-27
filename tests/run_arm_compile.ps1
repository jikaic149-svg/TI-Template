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
    "APP\Algorithm\IMUFusion\include",
    "APP\Algorithm\Mahony\include",
    "Device\Sensor\IMU",
    "Device\Sensor\IMU\ICM45686",
    "Device\Sensor\IMU\BMI088",
    "Mode\Communicate\include"
)
$includeArguments = @()
foreach ($include in $includes) {
    $includeArguments += @("-I", (Join-Path $workspaceRoot $include))
}
$sources = @(
    "APP\Algorithm\IMUFusion\src\algorithm_imu_fusion.c",
    "Mode\Communicate\Bluetooth\mode_communicate_bluetooth_pc.c",
    "Mode\Communicate\Bluetooth\mode_communicate_bluetooth_dual_mcu.c",
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
