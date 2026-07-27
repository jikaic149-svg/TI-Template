[CmdletBinding()]
param(
    [string]$OutputDirectory
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

Invoke-HostTest -Name "algorithm_imu_fusion" -Includes @(
    "APP\Algorithm\IMUFusion\include",
    "APP\Algorithm\Mahony\include"
) -Sources @(
    "APP\Algorithm\IMUFusion\src\algorithm_imu_fusion.c",
    "tests\imu_fusion\test_algorithm_imu_fusion.c"
)

Invoke-HostTest -Name "mode_communicate" -Includes @(
    "Mode\Communicate\include"
) -Sources @(
    "Mode\Communicate\Bluetooth\mode_communicate_bluetooth_pc.c",
    "Mode\Communicate\Bluetooth\mode_communicate_bluetooth_dual_mcu.c",
    "tests\mode_communicate\test_mode_communicate.c"
)

Invoke-HostTest -Name "single_imu" -Includes @(
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

Invoke-HostTest -Name "lingke_m0" -Includes @(
    "Device\Sensor\IMU\LingkeM0"
) -Sources @(
    "Device\Sensor\IMU\LingkeM0\device_lingke_m0.c",
    "tests\lingke_m0\test_device_lingke_m0.c"
)

Invoke-HostTest -Name "fused_navigation_mode" -Includes @(
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

Invoke-HostTest -Name "line_tracking_uart" -Includes @(
    "APP\Algorithm\LineTracking\include",
    "APP\Algorithm\PID\include",
    "BSP",
    "Device\Sensor\LineTracking",
    "Device\Sensor\LineTracking\XinliuT8",
    "Mode\Car\include",
    "Mode\Car\src"
) -Sources @(
    "APP\Algorithm\LineTracking\src\algorithm_line_tracking.c",
    "APP\Algorithm\PID\src\algorithm_pid.c",
    "Device\Sensor\LineTracking\device_line_tracking.c",
    "Device\Sensor\LineTracking\XinliuT8\device_xinliu_t8.c",
    "Mode\Car\src\mode_car_common.c",
    "Mode\Car\Tracking\mode_car_tracking.c",
    "Mode\Car\Tracking\mode_car_digital_tracking.c",
    "Mode\Car\Tracking\mode_car_analog_tracking.c",
    "Mode\Car\Control\mode_car_angle_controller.c",
    "Mode\Car\Control\mode_car_speed_controller.c",
    "tests\line_tracking_uart\test_line_tracking_uart.c"
)
