[CmdletBinding()]
param(
    [string]$OutputDirectory,
[ValidateSet("All", "Watchdog", "Communication", "IMU", "Motor", "Display", "Navigation", "LineTracking", "SpeedProfile", "WheelAcceleration")]
    [string[]]$Scope = @("All")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$knownGcc = "C:\msys64\ucrt64\bin\gcc.exe"
$gccCommand = Get-Command gcc -ErrorAction SilentlyContinue
$gcc = if ($null -ne $gccCommand) {
    $gccCommand.Source
}
elseif (Test-Path -LiteralPath $knownGcc -PathType Leaf) {
    $knownGcc
}
else {
    throw "Host GCC was not found in PATH or at $knownGcc"
}
$gccDirectory = Split-Path -Parent $gcc
$env:Path = $gccDirectory + ";" + $env:Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $workspaceRoot "build\host-tests"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$selectedScopes = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($item in $Scope) {
    [void]$selectedScopes.Add($item)
}

function Test-ScopeEnabled {
    param([Parameter(Mandatory = $true)][string]$Name)

    return $selectedScopes.Contains("All") -or $selectedScopes.Contains($Name)
}

function Invoke-HostTest {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string[]]$Includes,
        [Parameter(Mandatory = $true)][string[]]$Sources
    )

    $output = Join-Path $OutputDirectory ($Name + ".exe")
    $arguments = @(
        "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror"
    )
    foreach ($include in $Includes) {
        $arguments += @("-I", (Join-Path $workspaceRoot $include))
    }
    foreach ($source in $Sources) {
        $arguments += (Join-Path $workspaceRoot $source)
    }
    $arguments += @("-lm", "-o", $output)

    & $gcc @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name compile failed with exit code $LASTEXITCODE"
    }
    & $output
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

if (Test-ScopeEnabled -Name "Watchdog") {
    Invoke-HostTest -Name "app_watchdog" -Includes @(
        "APP\System\Watchdog"
    ) -Sources @(
        "APP\System\Watchdog\app_watchdog.c",
        "tests\watchdog\test_app_watchdog.c"
    )
}

if (Test-ScopeEnabled -Name "IMU") {
    Invoke-HostTest -Name "jy61p" -Includes @(
        "Device\Sensor\IMU\JY61P"
    ) -Sources @(
        "Device\Sensor\IMU\JY61P\device_jy61p.c",
        "tests\jy61p\test_device_jy61p.c"
    )

    Invoke-HostTest -Name "algorithm_imu_fusion" -Includes @(
        "config",
        "APP\Algorithm\IMUFusion\include",
        "APP\Algorithm\Mahony\include"
    ) -Sources @(
        "APP\Algorithm\IMUFusion\src\algorithm_imu_fusion.c",
        "tests\imu_fusion\test_algorithm_imu_fusion.c"
    )
}

if (Test-ScopeEnabled -Name "Communication") {
    Invoke-HostTest -Name "device_peer_link" -Includes @(
        "APP\Algorithm\Packet\include",
        "BSP",
        "Device\Communication\PeerLink"
    ) -Sources @(
        "APP\Algorithm\Packet\src\algorithm_packet.c",
        "Device\Communication\PeerLink\device_peer_link.c",
        "tests\mcu_peer_link\test_device_peer_link.c"
    )

    Invoke-HostTest -Name "mode_communicate" -Includes @(
        "Mode\Communicate\include"
    ) -Sources @(
        "Mode\Communicate\Bluetooth\mode_communicate_bluetooth_pc.c",
        "Mode\Communicate\Bluetooth\mode_communicate_bluetooth_dual_mcu.c",
        "Mode\Communicate\LoRa\mode_communicate_lora_half_duplex.c",
        "tests\mode_communicate\test_mode_communicate.c"
    )

    Invoke-HostTest -Name "device_lora" -Includes @(
        "Device\Communication\Serial",
        "Device\Communication\LoRa"
    ) -Sources @(
        "Device\Communication\LoRa\device_lora.c",
        "tests\lora\test_device_lora.c"
    )
}

if (Test-ScopeEnabled -Name "IMU") {
    Invoke-HostTest -Name "single_imu" -Includes @(
        "config",
        "APP\Algorithm\Mahony\include",
        "Device\Sensor\IMU",
        "Device\Sensor\IMU\ICM45686",
        "Device\Sensor\IMU\BMI088"
    ) -Sources @(
        "APP\Algorithm\Mahony\src\algorithm_mahony.c",
        "Device\Sensor\IMU\device_imu.c",
        "Device\Sensor\IMU\ICM45686\device_icm45686.c",
        "Device\Sensor\IMU\BMI088\device_bmi088.c",
        "tests\single_imu\test_single_imu.c"
    )
}

if (Test-ScopeEnabled -Name "Motor") {
    Invoke-HostTest -Name "motor_at8236" -Includes @(
        "BSP"
    ) -Sources @(
        "tests\motor_at8236\test_bsp_motor_at8236.c"
    )

    Invoke-HostTest -Name "motor_tb6612" -Includes @(
        "BSP"
    ) -Sources @(
        "tests\motor_tb6612\test_bsp_motor_tb6612.c"
    )

    Invoke-HostTest -Name "motor_models" -Includes @(
        "Device\Actuator\Motor\MG310",
        "Device\Actuator\Motor\MG513X"
    ) -Sources @(
        "tests\motor_models\test_motor_models.c"
    )
}

if (Test-ScopeEnabled -Name "Display") {
    Invoke-HostTest -Name "device_st7735s" -Includes @(
        "BSP",
        "Device\Display\ST7735S"
    ) -Sources @(
        "Device\Display\ST7735S\device_st7735s.c",
        "tests\display_st7735s\test_device_st7735s.c"
    )
}

if (Test-ScopeEnabled -Name "IMU") {
    Invoke-HostTest -Name "lingke_m0" -Includes @(
        "Device\Sensor\IMU\LingkeM0"
    ) -Sources @(
        "Device\Sensor\IMU\LingkeM0\device_lingke_m0.c",
        "tests\lingke_m0\test_device_lingke_m0.c"
    )
}

if (Test-ScopeEnabled -Name "Navigation") {
    Invoke-HostTest -Name "fused_navigation_mode" -Includes @(
        "config",
        "APP\Algorithm\PID\include",
        "Mode\Car\include",
        "Mode\Car\src"
    ) -Sources @(
        "APP\Algorithm\PID\src\algorithm_pid.c",
        "Mode\Car\src\mode_car_common.c",
        "Mode\Car\src\mode_car_curve.c",
        "Mode\Car\Spin\mode_car_spin.c",
        "Mode\Car\Control\mode_car_speed_controller.c",
        "Mode\Car\Navigation\mode_car_fused_navigation.c",
        "tests\fused_navigation_mode\test_mode_car_fused_navigation.c"
    )
}

if (Test-ScopeEnabled -Name "SpeedProfile") {
    Invoke-HostTest -Name "speed_profile" -Includes @(
        "config",
        "APP\Algorithm\PID\include",
        "Mode\Car\include"
    ) -Sources @(
        "APP\Algorithm\PID\src\algorithm_pid.c",
        "Mode\Car\Control\mode_car_speed_profile.c",
        "tests\speed_profile\test_mode_car_speed_profile.c"
    )
}

if (Test-ScopeEnabled -Name "WheelAcceleration") {
    Invoke-HostTest -Name "wheel_acceleration" -Includes @(
        "APP\Algorithm\Filter\include"
    ) -Sources @(
        "APP\Algorithm\Filter\src\algorithm_filter_wheel_acceleration.c",
        "tests\wheel_acceleration\test_algorithm_filter_wheel_acceleration.c"
    )
}

if (Test-ScopeEnabled -Name "LineTracking") {
    Invoke-HostTest -Name "line_tracking_uart" -Includes @(
        "config",
        "APP\Algorithm\LineTracking\include",
        "APP\Algorithm\PID\include",
        "BSP",
        "Device\Sensor\LineTracking",
        "Device\Sensor\LineTracking\XinliuT8",
        "Device\Sensor\LineTracking\YahboomTrack",
        "Device\Sensor\LineTracking\Ganwei",
        "Mode\Car\include",
        "Mode\Car\src"
    ) -Sources @(
        "APP\Algorithm\LineTracking\src\algorithm_line_tracking.c",
        "APP\Algorithm\PID\src\algorithm_pid.c",
        "Device\Sensor\LineTracking\device_line_tracking.c",
        "Device\Sensor\LineTracking\XinliuT8\device_xinliu_t8.c",
        "Device\Sensor\LineTracking\YahboomTrack\device_yahboom_track.c",
        "Device\Sensor\LineTracking\Ganwei\device_ganwei.c",
        "Mode\Car\src\mode_car_common.c",
        "Mode\Car\Tracking\mode_car_tracking.c",
        "Mode\Car\Tracking\mode_car_digital_tracking.c",
        "Mode\Car\Tracking\mode_car_analog_tracking.c",
        "Mode\Car\Control\mode_car_angle_controller.c",
        "Mode\Car\Control\mode_car_speed_controller.c",
        "tests\line_tracking_uart\test_line_tracking_uart.c"
    )
}

Write-Host ("Host GCC tests ({0}): PASS" -f ($Scope -join ","))
