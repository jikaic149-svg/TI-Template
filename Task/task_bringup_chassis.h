#ifndef TASK_BRINGUP_CHASSIS_H
#define TASK_BRINGUP_CHASSIS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Chassis bring-up (H-title): while parked, a long key press cycles contest
 * items 2..6 and a short press confirms the displayed item. Items 2, 5 and 6
 * start the one-lap route. Item 2 uses the complete Yahboom grayscale steering
 * output without INS lookahead or absolute-yaw steering. In the bounded final
 * window, a two-frame wide-black marker changes its goal to marker odometry +
 * 180 mm; mean encoder distance 6141.6 mm remains the missed-marker fallback.
 * A prolonged missing line
 * requests the same planned stop. The M2 wheel-speed half difference and each
 * final wheel target remain bounded. Item 4
 * is an isolated A-to-B yaw-hold test: the
 * angle PID holds the launch ICM yaw around the longitudinal profile and
 * stops at 1500 mm without grayscale or route-lookahead steering. Item 3 keeps the chassis locked for the
 * stationary ball test. Items 5
 * and 6 likewise pass A before their one-car-length planned stop. A short press while moving
 * requests a planned stop; a long press while moving cannot switch items.
 * The default H-title route uses 500 mm/s on the two
 * 1.5 m straight sections and 380 mm/s on the two semicircles; both values
 * are configuration-only starting points and must be calibrated on the final
 * chassis. On M2, the final marker approach reaches 220 mm/s while retaining
 * the grayscale steering differential; after marker confirmation, a bounded
 * launch-yaw correction is added during braking. The marker requires at least six raw channels >=900 and an
 * eight-channel average >=1700 for two consecutive new frames after a normal
 * frame inside the 5800 +/-300 mm search window.
 * The profile ramps acceleration and deceleration linearly, uses the
 * mean left/right encoder distance for its braking envelope, and only then
 * lets the existing dual-wheel speed loop generate motor effort. The ICM-45686 bias-corrected gyro is fused with
 * differential encoder increments into a local INS pose; Yahboom UART analog
 * frames provide lateral line-PID guidance. M2 does not consume INS steering;
 * moving items bridge a short gap and request
 * the same planned stop after a longer gap instead of jumping to zero. The next key press
 * requests a forward-planned stop; after stable zero speed it disables both
 * chassis motors. Every motor command stays inside the TB6612 protection clamp.
 * In the same 10 ms Task, PeerLink sends one minimal CAR Latest Value to F407:
 * mean measured chassis speed, ICM45686 body X/Z specific force, bias-corrected
 * body Y/Pitch rate, sample time and validity. JY61P, grayscale, odometry,
 * VOFA and F407/QD4310 CAN details are not carried by this frame.
 * Telemetry streams
 * continuously over the Bluetooth UART as VOFA+ FireWater ASCII lines
 * (comma-separated, \n terminated) with 32 debug values per line:
 *   0 route distance mm         1/2 measured left/right speed mm/s
 *   3/4 target left/right speed mm/s
 *   5 reference speed mm/s      6 reference acceleration mm/s^2
 *   7 remaining distance mm     8 braking trigger distance mm
 *   9 yaw deg                   10 normalized line error (-1..1)
 *   11 profile state: 0 idle, 1 acceleration, 2 cruise, 3 brake, 4 stopped
 *   12 absolute rod pitch deg     13 JY61P Euler sample age ms
 *   14..21 Yahboom raw ADC x1..x8
 *   22 F407 processed chassis acceleration mm/s^2
 *   23 F407 compensation rod angle mdeg
 *   24 F407 final rod command mdeg
 *   25/26 QD4310 target/feedback raw count
 *   27/28 QD4310 command/feedback current mA
 *   29 QD4310 speed 0.01 rpm      30 F407 control phase
 *   31 F407 status flags; bit9 means the DBG sample is fresh on G3519
 * The WIT/JY61P is read from a BSP-provided auxiliary serial transport and is
 * diagnostic only: CH12 = ICM45686 chassis Pitch + JY61P rod-relative Pitch.
 * If either Pitch is invalid, CH12=0 and CH13=9999. Neither value enters
 * chassis navigation, PeerLink, F407, or any motor control.
 * The RGB pixel shows idle/running/fault. At startup the G3519 ST7735S is
 * initialized once and filled red. No ball control runs on this MCU.
 *
 * Fault codes (g_BRINGUP_Fault): 0 none, 1 RGB, 2 key, 3 motor, 4 encoder,
 * 5 Bluetooth serial, 6 IMU, 7 task creation, 8 grayscale serial,
 * 9 speed controller, 10 analog line estimator/controller, 11 longitudinal
 * profile, 12 fused INS navigation.
 */
extern volatile uint32_t g_BRINGUP_Fault;

/* Display-only status: 0 success, 1 default configuration, 2 panel
 * initialization, 3 geometry query, 4 startup fill/unused-area clear,
 * 5 status-line update. A TFT failure does not change g_BRINGUP_Fault or stop
 * chassis/communication. */
extern volatile uint32_t g_BRINGUP_DisplayFault;

/* Create the bring-up task after BSP_Init(); returns false only when the
 * FreeRTOS task itself could not be created. Device failures are reported
 * through g_BRINGUP_Fault and the red RGB indicator instead. */
bool Task_BringupChassis_Init(void);

#endif
