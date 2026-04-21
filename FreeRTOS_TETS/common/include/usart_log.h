#ifndef __USART_LOG_H
#define __USART_LOG_H

#include "stm32f4xx_hal.h"

void xxx_print_log_f(const char *format, ...);
void __logger_print_init(void);
void __logger_uart_tx_cplt_callback(UART_HandleTypeDef *huart);

#endif /* __USART_LOG_H */
