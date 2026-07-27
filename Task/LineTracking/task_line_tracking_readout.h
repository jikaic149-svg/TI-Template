#ifndef TASK_LINE_TRACKING_READOUT_H
#define TASK_LINE_TRACKING_READOUT_H

#include <stdbool.h>
#include <stdint.h>

#define TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT 8U

extern volatile uint16_t g_TASK_LineTrackingRaw[
    TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT];
extern volatile float g_TASK_LineTrackingNormalized[
    TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT];
extern volatile float g_TASK_LineTrackingStrength[
    TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT];
extern volatile float g_TASK_LineTrackingDifferenceRatio;
extern volatile float g_TASK_LineTrackingPosition;
extern volatile float g_TASK_LineTrackingError;
extern volatile float g_TASK_LineTrackingConfidence;
extern volatile float g_TASK_LineTrackingLeftIntent;
extern volatile float g_TASK_LineTrackingRightIntent;
extern volatile uint32_t g_TASK_LineTrackingStatus;
extern volatile uint32_t g_TASK_LineTrackingAcceptedFrames;
extern volatile uint32_t g_TASK_LineTrackingRejectedFrames;
extern volatile uint32_t g_TASK_LineTrackingQueryTimeouts;

bool Task_LineTrackingReadout_Init(void);
void Task_LineTrackingReadout_Update(uint32_t now_ms);
/* This commissioning Task never grants actuator output permission. */
bool Task_LineTrackingReadout_OutputEnabled(void);
bool Task_LineTrackingReadout_ControlFaulted(void);

#endif
