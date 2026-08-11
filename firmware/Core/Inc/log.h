//
// Created by me on 12/17/25.
//

#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

void log_init();
__attribute__((format(printf, 2, 3))) void
log_info(const char *logger, const char *format, ...);
void log_info_v(const char *logger, const char *format, va_list args);
void log_dma_irq();
void log_uart_irq();

#endif // LOG_H
