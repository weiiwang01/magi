//
// Created by weiiwang01 on 9/19/26.
//

#ifndef CONSOLE_H
#define CONSOLE_H

#include "fifo.h"
#include "stm32g0xx_hal.h"
#include "stream.h"
#include "stream_notify.h"

typedef struct console console_t;
typedef struct console_tap console_tap_t;

struct console
{
  console_tap_t *tap;
  uint8_t rx_buffer[128];
  fifo_t rx_fifo;
  uint8_t tx_buffer[128];
  fifo_t tx_fifo;
  UART_HandleTypeDef uart;
  DMA_HandleTypeDef dma;
  GPIO_TypeDef *gpio;
  uint32_t pins;
  uint32_t af;
  IRQn_Type irq;
  bool enabled;
  bool initialized;
  bool failed;
};

struct console_tap
{
  console_t *console;
  stream_t stream;
  stream_notify_t notify;
};

extern console_t *console_unit_1;
extern console_t *console_unit_2;
extern console_t *console_unit_3;

int console_init();

int console_disable(console_t *console);
int console_enable(console_t *console);
bool console_is_enabled(console_t *console);

int console_tap_init(console_tap_t *tap);
int console_tap_attach(console_tap_t *tap, console_t *console);
int console_tap_detach(console_tap_t *tap);

void console_uart_irq(console_t *console);
void console_dma_irq();

#endif // CONSOLE_H
