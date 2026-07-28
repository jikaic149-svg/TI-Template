[CmdletBinding()]
param(
    [ValidateSet("All", "Watchdog", "Communication", "IMU")]
    [string[]]$Scope = @("All"),
    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$armGcc = "D:\GCC\ARMGCC\10 2021.10\bin\arm-none-eabi-gcc.exe"
if (-not (Test-Path -LiteralPath $armGcc -PathType Leaf)) {
    throw "ARM GCC was not found at $armGcc"
}
$outputDirectory = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    Join-Path $workspaceRoot "build\arm-check"
}
else {
    [System.IO.Path]::GetFullPath($OutputDirectory)
}
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$selectedScopes = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($item in $Scope) {
    [void]$selectedScopes.Add($item)
}

function Test-ScopeEnabled {
    param([Parameter(Mandatory = $true)][string]$Name)

    return $selectedScopes.Contains("All") -or $selectedScopes.Contains($Name)
}

$includes = @(
    "config",
    "APP\System\Watchdog",
    "APP\Algorithm\IMUFusion\include",
    "APP\Algorithm\Mahony\include",
    "Device\Sensor\IMU",
    "Device\Sensor\IMU\ICM45686",
    "Device\Sensor\IMU\BMI088",
    "Device\Communication\Serial",
    "Device\Communication\LoRa",
    "Mode\Communicate\include"
)
$includeArguments = @()
foreach ($include in $includes) {
    $includeArguments += @("-I", (Join-Path $workspaceRoot $include))
}
$sources = [System.Collections.Generic.List[string]]::new()
if (Test-ScopeEnabled -Name "Watchdog") {
    $sources.Add("APP\System\Watchdog\app_watchdog.c")
}
if (Test-ScopeEnabled -Name "Communication") {
    $sources.Add("Mode\Communicate\Bluetooth\mode_communicate_bluetooth_pc.c")
    $sources.Add("Mode\Communicate\Bluetooth\mode_communicate_bluetooth_dual_mcu.c")
    $sources.Add("Mode\Communicate\LoRa\mode_communicate_lora_half_duplex.c")
    $sources.Add("Device\Communication\LoRa\device_lora.c")
}
if (Test-ScopeEnabled -Name "IMU") {
    $sources.Add("APP\Algorithm\IMUFusion\src\algorithm_imu_fusion.c")
    $sources.Add("Device\Sensor\IMU\ICM45686\device_icm45686.c")
    $sources.Add("Device\Sensor\IMU\BMI088\device_bmi088.c")
}

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

Write-Host ("ARM strict compile tests ({0}): PASS" -f ($Scope -join ","))
