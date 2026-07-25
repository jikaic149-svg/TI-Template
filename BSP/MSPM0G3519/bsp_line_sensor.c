#include "bsp_line_sensor.h"

#include <stddef.h>

#include "bsp_resource_map.h"

#define BSP_LINE_SENSOR_COUNT    1U
#define BSP_LINE_SENSOR_CHANNELS 8U
#define BSP_LINE_CLOCK_DELAY     (BSP_RESOURCE_CPU_CLOCK_HZ / 2000000U)

struct BSP_LineSensor_Resource {
    uint8_t unused;
};

static const struct BSP_LineSensor_Resource g_line_sensor = {0U};

void BSP_LineSensor_Init(void)
{
    DL_GPIO_clearPins(BSP_RESOURCE_LINE_PORT, BSP_RESOURCE_LINE_CLK);
}

size_t BSP_LineSensor_Count(void)
{
    return BSP_LINE_SENSOR_COUNT;
}

BSP_LineSensor_Handle BSP_LineSensor_GetHandle(size_t index)
{
    return (index == 0U) ? &g_line_sensor : NULL;
}

bool BSP_LineSensor_GetInfo(
    BSP_LineSensor_Handle sensor, BSP_LineSensor_Info *info)
{
    if ((sensor != &g_line_sensor) || (info == NULL)) {
        return false;
    }
    *info = (BSP_LineSensor_Info){0};
    info->channel_count = BSP_LINE_SENSOR_CHANNELS;
    for (size_t index = 0U; index < BSP_LINE_SENSOR_CHANNELS; ++index) {
        info->channel_kind[index] = BSP_LINE_SENSOR_CHANNEL_DIGITAL;
    }
    return true;
}

bool BSP_LineSensor_Read(
    BSP_LineSensor_Handle sensor, BSP_LineSensor_RawSample *sample)
{
    BSP_LineSensor_RawSample next = {0};

    if ((sensor != &g_line_sensor) || (sample == NULL)) {
        return false;
    }
    DL_GPIO_clearPins(BSP_RESOURCE_LINE_PORT, BSP_RESOURCE_LINE_CLK);
    for (size_t index = 0U; index < BSP_LINE_SENSOR_CHANNELS; ++index) {
        DL_GPIO_setPins(BSP_RESOURCE_LINE_PORT, BSP_RESOURCE_LINE_CLK);
        DL_Common_delayCycles(BSP_LINE_CLOCK_DELAY);
        next.value[index] =
            ((DL_GPIO_readPins(BSP_RESOURCE_LINE_PORT,
                  BSP_RESOURCE_LINE_DAT) & BSP_RESOURCE_LINE_DAT) != 0U)
                ? BSP_LINE_SENSOR_VALUE_MAX
                : BSP_LINE_SENSOR_VALUE_MIN;
        next.valid_mask |= (uint32_t)1U << index;
        DL_GPIO_clearPins(BSP_RESOURCE_LINE_PORT, BSP_RESOURCE_LINE_CLK);
        DL_Common_delayCycles(BSP_LINE_CLOCK_DELAY);
    }
    *sample = next;
    return true;
}
