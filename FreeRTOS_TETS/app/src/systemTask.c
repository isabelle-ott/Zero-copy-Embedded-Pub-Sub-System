#include "systemTask.h"
#include "sysLED_Task.h"
#include "usart_log.h"

#define SYSTEM_TASK_LED_STACK_WORDS   (128U)
#define SYSTEM_TASK_LED_PRIORITY      (1U)

StaticEventGroup_t g_systemInitEventGroupBuffer;
EventGroupHandle_t g_systemInitEventGroup = NULL;

static StaticTask_t g_ledTaskBuffer;
static StackType_t g_ledTaskStack[SYSTEM_TASK_LED_STACK_WORDS];
static TaskHandle_t g_ledTaskHandle = NULL;

static void SystemTask_Init(void);
static void SystemTask_CreateLedTask(void);

void SystemTask_Start(void *argument)
{
  (void)argument;

  SystemTask_InitEventGroup();
  SystemTask_Init();
  SystemTask_CreateTasks();
  SystemTask_SignalInitDone();

  vTaskDelete(NULL);
}

void SystemTask_InitEventGroup(void)
{
  if (g_systemInitEventGroup == NULL)
  {
    g_systemInitEventGroup = xEventGroupCreateStatic(&g_systemInitEventGroupBuffer);
  }
}

void SystemTask_SignalInitDone(void)
{
  if (g_systemInitEventGroup != NULL)
  {
    xEventGroupSetBits(g_systemInitEventGroup, SYSTEM_TASK_INIT_BIT);
  }
}

BaseType_t SystemTask_WaitInitDone(TickType_t timeoutTicks)
{
  if (g_systemInitEventGroup == NULL)
  {
    return pdFAIL;
  }

  return (xEventGroupWaitBits(g_systemInitEventGroup,
                              SYSTEM_TASK_INIT_BIT,
                              pdFALSE,
                              pdTRUE,
                              timeoutTicks) & SYSTEM_TASK_INIT_BIT) ? pdPASS : pdFAIL;
}

void SystemTask_CreateTasks(void)
{
  SystemTask_CreateLedTask();
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

static void SystemTask_Init(void)
{
  /* 预留给后续硬件初始化、外设初始化或资源初始化 */
  __logger_print_init();
}
