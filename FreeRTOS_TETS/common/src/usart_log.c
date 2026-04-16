#include "usart_log.h"
#include <stdint.h>
#include "FreeRTOS.h"
#include "led.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_def.h"
#include "stm32f4xx_hal_uart.h"
#include "usart.h"
#include "semphr.h"
#include "stdarg.h"
#include <stdio.h>
#include <string.h>

static char TX_buff[300];

HAL_StatusTypeDef status_test;
static inline HAL_StatusTypeDef uart_send(uint8_t *data, uint16_t size)
{
    status_test = HAL_UART_Transmit_DMA(&huart1, data, size);
    return status_test;
}

static StaticSemaphore_t TXmutex_buffer;
static SemaphoreHandle_t TXmutex = NULL;

void __logger_print_init(void)
{
    TXmutex = xSemaphoreCreateMutexStatic(&TXmutex_buffer);
}

void xxx_print_log_f(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    if(xSemaphoreTake(TXmutex, 3 * pdMS_TO_TICKS(100)) == pdTRUE)
    {
        int len = vsnprintf(TX_buff, sizeof(TX_buff), format, args);
        uart_send((uint8_t*)TX_buff, len);
        xSemaphoreGive(TXmutex);
    }else{
        D1_LED_set(1);
    }
}

