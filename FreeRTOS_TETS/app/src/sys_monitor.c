#include "sys_monitor.h"
#include "systemTask.h"
#include "usart_log.h"
#include "FreeRTOS.h"
#include "task.h"

#define SYSTEM_TASK_SYS_MONITOR_STACK_WORDS   (256U)
#define SYSTEM_TASK_SYS_MONITOR_PRIORITY      (1U)

static StaticTask_t g_sysMonitorTaskBuffer;
static StackType_t g_sysMonitorTaskStack[SYSTEM_TASK_SYS_MONITOR_STACK_WORDS];
static TaskHandle_t g_sysMonitorTaskHandle = NULL;

void SysMonitor_Task(void *argument)
{
    (void)argument;

    static char task_list_buffer[1024];  //使用static降低任务栈，放在.bss段

    for(;;)
    {
        // 抓取所有任务状态
        vTaskList(task_list_buffer);
        
        xxx_print_log_f("\r\n--- System Monitor [%u] ---\r\n", (unsigned int)HAL_GetTick());
        xxx_print_log_f("Name\t\tState\tPri\tStack\tNum\r\n");
        xxx_print_log_f("%s", task_list_buffer);
        
        // 顺便打印一下全局剩余 Heap 内存 (单位：Bytes)
        xxx_print_log_f("Free Heap: %u Bytes\r\n", (unsigned int)xPortGetFreeHeapSize());
        xxx_print_log_f("---------------------------\r\n");
        
        osDelay(10000);
    }
}


static void SystemTask_CreateSysMonitorTask(void)
{
  g_sysMonitorTaskHandle = xTaskCreateStatic(SysMonitor_Task,
                                              "SysMonitor_Task",
                                              SYSTEM_TASK_SYS_MONITOR_STACK_WORDS,
                                              NULL,
                                              SYSTEM_TASK_SYS_MONITOR_PRIORITY,
                                              g_sysMonitorTaskStack,
                                              &g_sysMonitorTaskBuffer);
}

void SysMonitor_Task_Start(void)
{
    SystemTask_CreateSysMonitorTask();
}