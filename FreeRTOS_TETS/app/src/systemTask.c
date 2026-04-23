#include "systemTask.h"
#include "FreeRTOS.h"
#include "sysLED_Task.h"
#include "usart_log.h"
#include "sd_log.h"
#include "sys_monitor.h"

StaticEventGroup_t g_systemInitEventGroupBuffer;
EventGroupHandle_t g_systemInitEventGroup = NULL;

static void SystemTask_Init(void);

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
  LED_Task_Start();
  SysMonitor_Task_Start();
}



static void SystemTask_Init(void)
{
  /* 预留给后续硬件初始化、外设初始化或资源初始化 */
  __logger_print_init();
  sd_log_init();
}
