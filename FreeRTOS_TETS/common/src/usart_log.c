#include "usart_log.h"
#include <stdint.h>
#include "led.h"
#include "usart.h"
#include "stdarg.h"
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"


#define LOGGER_TX_BUFFER_SIZE 200U
#define LOGGER_TX_BUFFER_COUNT 6U

typedef enum
{
    LOGGER_BUF_FREE = 0U,
    LOGGER_BUF_FILLING,
    LOGGER_BUF_READY,
    LOGGER_BUF_SENDING,
} logger_buf_state_t;

static char TX_buff[LOGGER_TX_BUFFER_COUNT][LOGGER_TX_BUFFER_SIZE];
static volatile logger_buf_state_t buf_state[LOGGER_TX_BUFFER_COUNT] =
{
    LOGGER_BUF_FREE,
    LOGGER_BUF_FREE,
    LOGGER_BUF_FREE,
    LOGGER_BUF_FREE,
};
static volatile uint16_t tx_len[LOGGER_TX_BUFFER_COUNT] = {0U, 0U, 0U, 0U};
static volatile uint8_t tx_busy = 0U;
static volatile uint8_t sending_buf = 0U;
static volatile uint8_t queue_head = 0U;
static volatile uint8_t queue_tail = 0U;

static inline HAL_StatusTypeDef uart_send(uint8_t *data, uint16_t size)
{
    return HAL_UART_Transmit_DMA(&huart1, data, size);
}

// 注意：此函数被调用时，必须已经处于临界区内
static void logger_try_start_next_locked(void)
{
    if (tx_busy != 0U)
    {
        return;
    }

    for (uint8_t i = 0U; i < LOGGER_TX_BUFFER_COUNT; ++i)
    {
        uint8_t idx = (uint8_t)((queue_head + i) % LOGGER_TX_BUFFER_COUNT);
        if (buf_state[idx] == LOGGER_BUF_READY)
        {
            buf_state[idx] = LOGGER_BUF_SENDING;
            sending_buf = idx;
            tx_busy = 1U;

            if (uart_send((uint8_t *)TX_buff[idx], tx_len[idx]) != HAL_OK)
            {
                buf_state[idx] = LOGGER_BUF_FREE;
                tx_len[idx] = 0U;
                tx_busy = 0U;
                D1_LED_set(1);
            }
            return;
        }
    }
}

void __logger_print_init(void)
{
    // 如果这个函数是在 osKernelStart 之前调用的，不需要加临界区
    // 如果是在任务里调用的，请使用 taskENTER_CRITICAL();
    for (uint8_t i = 0U; i < LOGGER_TX_BUFFER_COUNT; ++i)
    {
        buf_state[i] = LOGGER_BUF_FREE;
        tx_len[i] = 0U;
    }
    tx_busy = 0U;
    sending_buf = 0U;
    queue_head = 0U;
    queue_tail = 0U;
}

void xxx_print_log_f(const char *format, ...)
{
    uint8_t target_buf = LOGGER_TX_BUFFER_COUNT;

    // --- 第一阶段：寻找可用缓冲区 (需加锁) ---
    taskENTER_CRITICAL(); // 上锁
    for (uint8_t i = 0U; i < LOGGER_TX_BUFFER_COUNT; ++i)
    {
        uint8_t idx = (uint8_t)((queue_tail + i) % LOGGER_TX_BUFFER_COUNT);
        if (buf_state[idx] == LOGGER_BUF_FREE)
        {
            buf_state[idx] = LOGGER_BUF_FILLING;
            target_buf = idx;
            queue_tail = (uint8_t)((idx + 1U) % LOGGER_TX_BUFFER_COUNT);
            break;
        }
    }
    taskEXIT_CRITICAL(); // 解锁

    // 如果队列满了，直接返回 (此时锁已经解开，不会死机)
    if (target_buf >= LOGGER_TX_BUFFER_COUNT)
    {
        D1_LED_set(1);
        return;
    }

    // --- 第二阶段：格式化字符串 (耗时操作，不加锁！) ---
    va_list args;
    va_start(args, format);
    int len = vsnprintf(TX_buff[target_buf], sizeof(TX_buff[target_buf]), format, args);
    va_end(args);

    if (len < 0)
    {
        taskENTER_CRITICAL(); // 发生错误，重新加锁清理状态
        buf_state[target_buf] = LOGGER_BUF_FREE;
        tx_len[target_buf] = 0U;
        taskEXIT_CRITICAL();  // 解锁
        
        D1_LED_set(1);
        return;
    }

    if (len >= (int)sizeof(TX_buff[target_buf]))
    {
        len = (int)sizeof(TX_buff[target_buf]) - 1;
    }
    TX_buff[target_buf][len] = '\0';

    // --- 第三阶段：标记就绪并尝试触发 DMA (需加锁) ---
    taskENTER_CRITICAL(); // 上锁
    tx_len[target_buf] = (uint16_t)len;
    buf_state[target_buf] = LOGGER_BUF_READY;
    logger_try_start_next_locked();
    taskEXIT_CRITICAL(); // 解锁
}

void __logger_uart_tx_cplt_callback(UART_HandleTypeDef *huart)
{
    if (huart != &huart1)
    {
        return;
    }

    // 在中断服务函数(ISR)中，必须使用 FreeRTOS 专用的 _FROM_ISR 宏！
    UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();

    if (sending_buf < LOGGER_TX_BUFFER_COUNT)
    {
        buf_state[sending_buf] = LOGGER_BUF_FREE;
        tx_len[sending_buf] = 0U;
    }
    tx_busy = 0U;
    logger_try_start_next_locked();

    taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
}