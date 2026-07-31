[CmdletBinding()]
param(
    [ValidateSet(
        "All",
        "Watchdog",
        "Communication",
        "IMU",
        "Motor",
        "Display",
        "Navigation",
        "LineTracking",
        "SpeedProfile",
        "Gimbal",
        "Optional"
    )]
    [string[]]$Scope = @("All"),
    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$knownArmGcc = "D:\GCC\ARMGCC\10 2021.10\bin\arm-none-eabi-gcc.exe"
$armGccCommand = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
$armGcc = if ($null -ne $armGccCommand) {
    $armGccCommand.Source
}
elseif (Test-Path -LiteralPath $knownArmGcc -PathType Leaf) {
    $knownArmGcc
}
else {
    throw "ARM GCC was not found in PATH or at $knownArmGcc"
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
    "BSP",
    "APP\System\Watchdog",
    "APP\Algorithm\Packet\include",
    "APP\Algorithm\ReliableLink\include",
    "APP\Algorithm\ADRC\include",
    "APP\Algorithm\CRC\include",
    "APP\Algorithm\Filter\include",
    "APP\Algorithm\LQR\include",
    "APP\Algorithm\PID\include",
    "APP\Algorithm\LineTracking\include",
    "APP\Algorithm\IMUFusion\include",
    "APP\Algorithm\Mahony\include",
    "APP\Algorithm\SPSC_Ring\include",
    "Device\Input\Key",
    "Device\Actuator\LED",
    "Device\Actuator\Motor",
    "Device\Actuator\PowerSwitch",
    "Device\Actuator\QD4310",
    "Device\Actuator\RGB",
    "Device\Actuator\Servo",
    "Device\Display\ST7735S",
    "Device\System\Watchdog",
    "Device\Communication\Vision",
    "Device\Communication\PeerLink",
    "Device\Sensor\LineTracking",
    "Device\Sensor\LineTracking\XinliuT8",
    "Device\Sensor\LineTracking\YahboomTrack",
    "Device\Sensor\LineTracking\Ganwei",
    "Device\Sensor\Encoder",
    "Device\Sensor\IMU",
    "Device\Sensor\IMU\ICM45686",
    "Device\Sensor\IMU\BMI088",
    "Device\Sensor\IMU\DualIMU",
    "Device\Sensor\IMU\JY61P",
    "Device\Sensor\IMU\LingkeM0",
    "Device\Communication\Serial",
    "Device\Communication\LoRa",
    "Device\Storage\Flash",
    "Mode\Communicate\include",
    "Mode\Car\include",
    "Mode\Car\src",
    "Mode\Gimbal\include",
    "Mode\Gimbal\src"
)
$includeArguments = @()
foreach ($include in $includes) {
    $includeArguments += @("-I", (Join-Path $workspaceRoot $include))
}
$sources = [System.Collections.Generic.List[string]]::new()
if (Test-ScopeEnabled -Name "Watchdog") {
    $sources.Add("APP\System\Watchdog\app_watchdog.c")
    $sources.Add("Device\System\Watchdog\device_watchdog.c")
}
if (Test-ScopeEnabled -Name "Communication") {
    $sources.Add("Device\Communication\PeerLink\device_peer_link.c")
    $sources.Add("Mode\Communicate\Bluetooth\mode_communicate_bluetooth_pc.c")
    $sources.Add("Mode\Communicate\Bluetooth\mode_communicate_bluetooth_dual_mcu.c")
    $sources.Add("Mode\Communicate\LoRa\mode_communicate_lora_half_duplex.c")
    $sources.Add("Device\Communication\Serial\device_serial.c")
    $sources.Add("Device\Communication\LoRa\device_lora.c")
    $sources.Add("Device\Communication\Vision\device_vision.c")
}
if (Test-ScopeEnabled -Name "IMU") {
    $sources.Add("APP\Algorithm\Mahony\src\algorithm_mahony.c")
    $sources.Add("APP\Algorithm\IMUFusion\src\algorithm_imu_fusion.c")
    $sources.Add("Device\Sensor\IMU\device_imu.c")
    $sources.Add("Device\Sensor\IMU\ICM45686\device_icm45686.c")
    $sources.Add("Device\Sensor\IMU\ICM45686\device_icm45686_spi.c")
    $sources.Add("Device\Sensor\IMU\BMI088\device_bmi088.c")
    $sources.Add("Device\Sensor\IMU\BMI088\device_bmi088_spi.c")
    $sources.Add("Device\Sensor\IMU\DualIMU\device_dual_imu.c")
    $sources.Add("Device\Sensor\IMU\JY61P\device_jy61p.c")
    $sources.Add("Device\Sensor\IMU\LingkeM0\device_lingke_m0.c")
}
if (Test-ScopeEnabled -Name "Motor") {
    $sources.Add("Device\Actuator\Motor\device_motor.c")
}
if (Test-ScopeEnabled -Name "Display") {
    $sources.Add("Device\Display\ST7735S\device_st7735s.c")
}
if (Test-ScopeEnabled -Name "Navigation") {
    $sources.Add("APP\Algorithm\PID\src\algorithm_pid.c")
    $sources.Add("Mode\Car\src\mode_car_common.c")
    $sources.Add("Mode\Car\src\mode_car_idle.c")
    $sources.Add("Mode\Car\src\mode_car_straight.c")
    $sources.Add("Mode\Car\src\mode_car_turning.c")
    $sources.Add("Mode\Car\Spin\mode_car_spin.c")
    $sources.Add("Mode\Car\src\mode_car_curve.c")
    $sources.Add("Mode\Car\Navigation\mode_car_fused_navigation.c")
    $sources.Add("Mode\Car\Control\mode_car_speed_controller.c")
    $sources.Add("Mode\Car\Control\mode_car_angle_controller.c")
    $sources.Add("Mode\Car\src\mode_car_brake.c")
    $sources.Add("Mode\Car\src\mode_car_done.c")
}
if (Test-ScopeEnabled -Name "SpeedProfile") {
    $sources.Add("Mode\Car\Control\mode_car_speed_profile.c")
}
if (Test-ScopeEnabled -Name "LineTracking") {
    $sources.Add("APP\Algorithm\LineTracking\src\algorithm_line_tracking.c")
    $sources.Add("Device\Sensor\LineTracking\device_line_tracking.c")
    $sources.Add("Device\Sensor\LineTracking\XinliuT8\device_xinliu_t8.c")
    $sources.Add("Device\Sensor\LineTracking\YahboomTrack\device_yahboom_track.c")
    $sources.Add("Device\Sensor\LineTracking\Ganwei\device_ganwei.c")
    $sources.Add("Mode\Car\Tracking\mode_car_tracking.c")
    $sources.Add("Mode\Car\Tracking\mode_car_digital_tracking.c")
    $sources.Add("Mode\Car\Tracking\mode_car_analog_tracking.c")
}
if (Test-ScopeEnabled -Name "Gimbal") {
    $sources.Add("Mode\Gimbal\src\mode_gimbal_foundation.c")
    $sources.Add("Mode\Gimbal\src\mode_gimbal_stabilization.c")
    $sources.Add("Mode\Gimbal\src\mode_gimbal_vision.c")
    $sources.Add("Mode\Gimbal\src\mode_gimbal_calibration.c")
    $sources.Add("Mode\Gimbal\src\mode_gimbal_scheduler.c")
}
if (Test-ScopeEnabled -Name "Optional") {
    $sources.Add("APP\Algorithm\ADRC\src\algorithm_adrc.c")
    $sources.Add("APP\Algorithm\CRC\src\algorithm_crc.c")
    $sources.Add("APP\Algorithm\Filter\src\algorithm_filter_biquad.c")
    $sources.Add("APP\Algorithm\Filter\src\algorithm_filter_kalman.c")
    $sources.Add("APP\Algorithm\Filter\src\algorithm_filter_low_pass.c")
    $sources.Add("APP\Algorithm\Filter\src\algorithm_filter_median.c")
    $sources.Add("APP\Algorithm\Filter\src\algorithm_filter_moving_average.c")
    $sources.Add("APP\Algorithm\Filter\src\algorithm_filter_wheel_acceleration.c")
    $sources.Add("APP\Algorithm\LQR\src\algorithm_lqr.c")
    $sources.Add("APP\Algorithm\Packet\src\algorithm_packet.c")
    $sources.Add("APP\Algorithm\ReliableLink\src\algorithm_reliable_link.c")
    $sources.Add("APP\Algorithm\SPSC_Ring\src\algorithm_spsc_ring.c")
    $sources.Add("Device\Input\Key\device_key.c")
    $sources.Add("Device\Input\Key\device_key_input.c")
    $sources.Add("Device\Actuator\LED\device_led.c")
    $sources.Add("Device\Actuator\PowerSwitch\device_power_switch.c")
    $sources.Add("Device\Actuator\QD4310\device_qd4310.c")
    $sources.Add("Device\Actuator\RGB\device_rgb.c")
    $sources.Add("Device\Actuator\Servo\device_servo.c")
    $sources.Add("Device\Sensor\Encoder\device_encoder.c")
    $sources.Add("Device\Storage\Flash\device_flash.c")
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

Write-Host ("ARM strict compile tests ({0}, {1} source files): PASS" -f `
    ($Scope -join ","), $sources.Count)
