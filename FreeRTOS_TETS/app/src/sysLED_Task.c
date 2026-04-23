#include "sysLED_Task.h"
#include "cmsis_os2.h"
#include "systemTask.h"
#include "led.h"
#include <stdint.h>

#define SYSTEM_TASK_LED_STACK_WORDS   (128U)
#define SYSTEM_TASK_LED_PRIORITY      (1U)

static StaticTask_t g_ledTaskBuffer;
static StackType_t g_ledTaskStack[SYSTEM_TASK_LED_STACK_WORDS];
static TaskHandle_t g_ledTaskHandle = NULL;

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

static void SystemTask_CreateLedTask(void)
{
  g_ledTaskHandle = xTaskCreateStatic(LED_Toggle_Task,
                                      "LED_Toggle_Task",
                                      SYSTEM_TASK_LED_STACK_WORDS,
                                      NULL,
                                      SYSTEM_TASK_LED_PRIORITY,
                                      g_ledTaskStack,
                                      &g_ledTaskBuffer);
  configASSERT(g_ledTaskHandle != NULL);
}

void LED_Task_Start(void)
{
    SystemTask_CreateLedTask();
}