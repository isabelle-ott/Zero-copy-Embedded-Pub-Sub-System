#include "sysLED_Task.h"
#include "cmsis_os2.h"
#include "systemTask.h"
#include "led.h"


void LED_Toggle_Task(void *argument)
{
    (void)argument;

    if (SystemTask_WaitInitDone(SYSTEM_TASK_INIT_TIMEOUT) != pdPASS)
    {
        vTaskDelete(NULL);
    }

    for (;;)
    {
        led_toggle();
    }
}
