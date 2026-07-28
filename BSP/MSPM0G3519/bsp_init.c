#include "bsp_init.h"

#include "bsp_internal.h"
#include "bsp_led.h"
#include "bsp_rgb.h"
#include "bsp_watchdog.h"

void BSP_Init(void)
{
    (void)BSP_Watchdog_Init();
    BSP_PWM_Init();
    BSP_Motor_Init();
    BSP_Encoder_Init();
    BSP_UART_Init();
    BSP_SPI_Init();
    BSP_LineSensor_Init();
    BSP_LED_Init();
    BSP_RGB_Init();
    BSP_Servo_Init();
    BSP_PowerSwitch_Init();
    BSP_Buzzer_Init();
}
