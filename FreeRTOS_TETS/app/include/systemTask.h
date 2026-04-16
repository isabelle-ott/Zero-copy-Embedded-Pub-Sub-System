#ifndef __SYSTEM_TASK_H
#define __SYSTEM_TASK_H

#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_TASK_INIT_BIT      (1U << 0)
#define SYSTEM_TASK_INIT_TIMEOUT   pdMS_TO_TICKS(5000U)

extern EventGroupHandle_t g_systemInitEventGroup;

BaseType_t SystemTask_WaitInitDone(TickType_t timeoutTicks);
void SystemTask_SignalInitDone(void);
void SystemTask_InitEventGroup(void);
void SystemTask_CreateTasks(void);

void SystemTask_Start(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* __SYSTEM_TASK_H */