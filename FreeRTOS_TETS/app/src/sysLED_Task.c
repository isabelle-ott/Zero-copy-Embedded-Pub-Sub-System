#include "sysLED_Task.h"
#include "cmsis_os2.h"
#include "stm32f4xx_hal.h"
#include "systemTask.h"
#include "led.h"
#include "usart_log.h"
#include <stdint.h>


void LED_Toggle_Task(void *argument)
{
    (void)argument;

    if (SystemTask_WaitInitDone(SYSTEM_TASK_INIT_TIMEOUT) != pdPASS)
    {
        vTaskDelete(NULL);
    }

    for (;;)
    {
        static uint32_t tick = 0;
        tick = HAL_GetTick()-1;
        led_toggle();
        xxx_print_log_f("Tick is %d\r\n", tick);
    }
}
