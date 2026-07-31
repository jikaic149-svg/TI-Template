#ifndef TASK_BRINGUP_CHASSIS_H
#define TASK_BRINGUP_CHASSIS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Chassis bring-up (H-title): while parked, a long key press cycles contest
 * items 2..6 and a short press confirms the displayed item. Items 2, 5 and 6
 * start the one-lap route. Before its finish marker, item 2 uses the complete
 * Yahboom grayscale steering output without INS lookahead or absolute-yaw steering. In the bounded final
 * window, a two-frame wide-black marker changes its goal to marker odometry +
 * 180 mm; mean encoder distance 6141.6 mm remains the missed-marker fallback.
 * A prolonged missing line
 * requests the same planned stop. The M2 wheel-speed half difference and each
 * final wheel target remain bounded. Item 4
 * is an A-to-B yaw-hold test: the angle PID holds the launch ICM yaw around a
 * symmetric 100 mm/s^2, 400 mm/s^3 jerk-limited profile capped at 320 mm/s.
 * Its goal stays 50 mm beyond B so the scoring point is crossed without
 * grayscale or route-lookahead steering. Item 3 keeps the chassis locked for
 * the stationary ball test. Items 5 and 6 use infrared
 * grayscale steering around a constant 234 mm/s longitudinal cap with
 * symmetric 80 mm/s^2 ramps and 320 mm/s^3 jerk. A 70 mm/s half-difference bound and
 * 3000 mm/s^2 slew keep grayscale wheel targets continuous without delaying
 * curve entry. After the front sensor sees A, they keep grayscale guidance,
 * add launch-yaw correction and use the same 180 mm planned runout as M2; the
 * measured 230 mm sensor offset remains the score-time reference. A short
 * press while moving
 * requests a planned stop; a long press while moving cannot switch items.
 * M2 uses 500 mm/s on the two 1.5 m straight sections and 380 mm/s on the two
 * semicircles; both values are configuration-only starting points and must be
 * calibrated on the final chassis. On M2, the final marker approach reaches
 * 220 mm/s while retaining
 * the grayscale steering differential; after marker confirmation, a bounded
 * launch-yaw correction is added during braking; M5/M6 use the same correction
 * after their marker confirmation. The marker requires at least six raw
 * channels >=900 and an
 * eight-channel average >=1700 for two consecutive new frames after a normal
 * frame inside the 5800 +/-300 mm search window.
 * The profile jerk-limits acceleration and deceleration, uses the
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
 *   12 filtered mean-wheel acceleration mm/s^2, two decimals
 *   13 applied closed-loop acceleration correction mm/s^2, two decimals
 *   14..21 Yahboom raw ADC x1..x8
 *   22 F407 processed chassis acceleration mm/s^2
 *   23 F407 compensation rod angle mdeg
 *   24 F407 final rod command mdeg
 *   25/26 QD4310 target/feedback raw count
 *   27/28 QD4310 command/feedback current mA
 *   29 QD4310 speed 0.01 rpm      30 F407 control phase
 *   31 F407 status flags; bit9 means the DBG sample is fresh on G3519
 * The WIT/JY61P auxiliary serial initialization is disabled. Its independent
 * rod-calibration cache is not part of the fixed 32-channel VOFA frame.
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

typedef enum {
    TASK_BRINGUP_BALL_COMMAND_IDLE = 0,
    TASK_BRINGUP_BALL_COMMAND_WAIT_LINK,
    TASK_BRINGUP_BALL_COMMAND_BUSY_RETRY,
    TASK_BRINGUP_BALL_COMMAND_QUEUED,
    TASK_BRINGUP_BALL_COMMAND_INVALID,
    TASK_BRINGUP_BALL_COMMAND_BUSY_EXHAUSTED,
    TASK_BRINGUP_BALL_COMMAND_SEND_ERROR,
} Task_BringupBallCommandStatus;

/* Atomic MCP debug mailbox. High 16 bits are a nonzero request ID. The low
 * signed 16 bits are a target in 0.1 mm (-1200..1200), or INT16_MIN for
 * 0 A -> Disable. The Task never clears this word; reuse a new request ID for
 * every submission. QUEUED proves only local UART enqueue, not F407 execution. */
extern volatile uint32_t g_BRINGUP_BallCommandMailbox;
extern volatile uint32_t g_BRINGUP_BallCommandLastQueued;
extern volatile uint32_t g_BRINGUP_BallCommandStatus;
extern volatile uint32_t g_BRINGUP_BallCommandQueueAttempts;

#define TASK_BRINGUP_ROD_CALIBRATION_CAPACITY 2500U

typedef struct {
    uint32_t sample_ms;
    uint16_t motor_target_raw;
    uint16_t motor_feedback_raw;
    /* JY61P 0x53 order: Roll, Pitch, Yaw, each in 0.01 degree. */
    int16_t jy61_euler_centi_deg[3];
} Task_BringupRodCalibrationSample;

typedef enum {
    TASK_BRINGUP_ROD_CALIBRATION_IDLE = 0,
    TASK_BRINGUP_ROD_CALIBRATION_RECORDING,
    TASK_BRINGUP_ROD_CALIBRATION_COMPLETE,
    TASK_BRINGUP_ROD_CALIBRATION_FULL,
} Task_BringupRodCalibrationState;

/* SWD-readable, Task-owned calibration capture. The first fresh F407 phase 2
 * frame starts and clears a run, subsequent phase-2 frames continuously store
 * synchronized Euler/raw samples, and phase 0 freezes the completed data. */
extern volatile Task_BringupRodCalibrationSample
    g_BRINGUP_RodCalibrationSamples[
        TASK_BRINGUP_ROD_CALIBRATION_CAPACITY];
extern volatile uint32_t g_BRINGUP_RodCalibrationCount;
extern volatile uint32_t g_BRINGUP_RodCalibrationPointCount;
extern volatile uint32_t g_BRINGUP_RodCalibrationInvalidEulerCount;
extern volatile uint32_t g_BRINGUP_RodCalibrationState;

/* Create the bring-up task after BSP_Init(); returns false only when the
 * FreeRTOS task itself could not be created. Device failures are reported
 * through g_BRINGUP_Fault and the red RGB indicator instead. */
bool Task_BringupChassis_Init(void);

#endif
