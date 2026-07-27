#ifndef TASK_ANALOG_TRACKING_H
#define TASK_ANALOG_TRACKING_H

#include <stdbool.h>
#include <stdint.h>

#define TASK_ANALOG_TRACKING_CHANNEL_COUNT 8U

extern volatile uint16_t g_TASK_AnalogTrackingRaw[
    TASK_ANALOG_TRACKING_CHANNEL_COUNT];
extern volatile float g_TASK_AnalogTrackingDifferenceRatio;
extern volatile float g_TASK_AnalogTrackingPosition;
extern volatile float g_TASK_AnalogTrackingError;
extern volatile float g_TASK_AnalogTrackingConfidence;
extern volatile float g_TASK_AnalogTrackingSteeringErrorDeg;
extern volatile float g_TASK_AnalogTrackingTargetYawDeg;
extern volatile float g_TASK_AnalogTrackingMeasuredYawDeg;
extern volatile float g_TASK_AnalogTrackingTargetSpeedA;
extern volatile float g_TASK_AnalogTrackingMeasuredSpeedA;
extern volatile float g_TASK_AnalogTrackingEffortA;
extern volatile float g_TASK_AnalogTrackingTargetSpeedB;
extern volatile float g_TASK_AnalogTrackingMeasuredSpeedB;
extern volatile float g_TASK_AnalogTrackingEffortB;
extern volatile uint32_t g_TASK_AnalogTrackingStatus;
extern volatile uint32_t g_TASK_AnalogTrackingFault;
extern volatile uint32_t g_TASK_AnalogTrackingUsable;
extern volatile uint32_t g_TASK_AnalogTrackingAcceptedFrames;
extern volatile uint32_t g_TASK_AnalogTrackingRejectedFrames;
extern volatile uint32_t g_TASK_AnalogTrackingQueryTimeouts;

bool Task_AnalogTracking_Init(void);
void Task_AnalogTracking_Update(uint32_t now_ms);
bool Task_AnalogTracking_OutputEnabled(void);
bool Task_AnalogTracking_ControlFaulted(void);

#endif
