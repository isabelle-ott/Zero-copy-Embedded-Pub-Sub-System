#include "led.h"
#include "main.h"
#include "cmsis_os2.h"
#include <stdint.h>

#define LED_TASK_PERIOD_MS   (1000U)

void led_toggle(void)
{
    HAL_GPIO_TogglePin(sysLED_GPIO_Port, sysLED_Pin);
    osDelay(LED_TASK_PERIOD_MS);
}

void D1_LED_set(int8_t status)
{
    switch (status) {
        case 0:
            HAL_GPIO_WritePin(BUG_LED_GPIO_Port, BUG_LED_Pin, GPIO_PIN_RESET);
            break;
        case 1:
            HAL_GPIO_WritePin(BUG_LED_GPIO_Port, BUG_LED_Pin, GPIO_PIN_SET);
            break;
        case -1:
            HAL_GPIO_TogglePin(BUG_LED_GPIO_Port, BUG_LED_Pin);
    }
}