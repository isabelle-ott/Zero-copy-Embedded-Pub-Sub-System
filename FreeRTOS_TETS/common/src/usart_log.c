#include "usart_log.h"
#include <stdint.h>
#include "led.h"
#include "usart.h"
#include "stdarg.h"
#include <stdio.h>
#include <string.h>

#define LOGGER_TX_BUFFER_SIZE 300U
#define LOGGER_TX_BUFFER_COUNT 4U

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
static volatile uint8_t tx_len[LOGGER_TX_BUFFER_COUNT] = {0U, 0U, 0U, 0U};
static volatile uint8_t tx_busy = 0U;
static volatile uint8_t sending_buf = 0U;
static volatile uint8_t queue_head = 0U;
static volatile uint8_t queue_tail = 0U;

static inline HAL_StatusTypeDef uart_send(uint8_t *data, uint16_t size)
{
    return HAL_UART_Transmit_DMA(&huart1, data, size);
}

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

static void logger_release_oldest_ready_slot_locked(void)
{
    for (uint8_t i = 0U; i < LOGGER_TX_BUFFER_COUNT; ++i)
    {
        uint8_t idx = (uint8_t)((queue_head + i) % LOGGER_TX_BUFFER_COUNT);
        if (buf_state[idx] == LOGGER_BUF_READY)
        {
            buf_state[idx] = LOGGER_BUF_FREE;
            tx_len[idx] = 0U;
            queue_head = (uint8_t)((idx + 1U) % LOGGER_TX_BUFFER_COUNT);
            return;
        }
    }
}

void __logger_print_init(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    for (uint8_t i = 0U; i < LOGGER_TX_BUFFER_COUNT; ++i)
    {
        buf_state[i] = LOGGER_BUF_FREE;
        tx_len[i] = 0U;
    }
    tx_busy = 0U;
    sending_buf = 0U;
    queue_head = 0U;
    queue_tail = 0U;

    if (primask == 0U)
    {
        __enable_irq();
    }
}

void xxx_print_log_f(const char *format, ...)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint8_t target_buf = LOGGER_TX_BUFFER_COUNT;
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

    if (primask == 0U)
    {
        __enable_irq();
    }

    if (target_buf >= LOGGER_TX_BUFFER_COUNT)
    {
        D1_LED_set(1);
        return;
    }

    va_list args;
    va_start(args, format);
    int len = vsnprintf(TX_buff[target_buf], sizeof(TX_buff[target_buf]), format, args);
    va_end(args);

    if (len < 0)
    {
        primask = __get_PRIMASK();
        __disable_irq();
        buf_state[target_buf] = LOGGER_BUF_FREE;
        tx_len[target_buf] = 0U;
        if (primask == 0U)
        {
            __enable_irq();
        }
        D1_LED_set(1);
        return;
    }

    if (len >= (int)sizeof(TX_buff[target_buf]))
    {
        len = (int)sizeof(TX_buff[target_buf]) - 1;
    }

    TX_buff[target_buf][len] = '\0';
    tx_len[target_buf] = (uint16_t)len;

    primask = __get_PRIMASK();
    __disable_irq();
    buf_state[target_buf] = LOGGER_BUF_READY;
    logger_try_start_next_locked();
    if (primask == 0U)
    {
        __enable_irq();
    }
}

void __logger_uart_tx_cplt_callback(UART_HandleTypeDef *huart)
{
    if (huart != &huart1)
    {
        return;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    if (sending_buf < LOGGER_TX_BUFFER_COUNT)
    {
        buf_state[sending_buf] = LOGGER_BUF_FREE;
        tx_len[sending_buf] = 0U;
    }
    tx_busy = 0U;
    logger_try_start_next_locked();

    if (primask == 0U)
    {
        __enable_irq();
    }
}

