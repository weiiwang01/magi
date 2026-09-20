#ifndef STREAM_NOTIFY_H
#define STREAM_NOTIFY_H

#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32g0xx.h"

typedef struct
{
  TaskHandle_t task;
  TaskHandle_t waiter;
  uint32_t read;
  uint32_t write;
} stream_notify_t;

static void
stream_notify_set(stream_notify_t *notify, uint32_t read, uint32_t write)
{
  notify->task = (read | write) ? xTaskGetCurrentTaskHandle() : NULL;
  notify->read = read;
  notify->write = write;
}

static void
stream_notify_send(stream_notify_t *notify, bool read, bool write)
{
  uint32_t bits = (read ? notify->read : 0) | (write ? notify->write : 0);
  if (__get_IPSR() != 0U)
    {
      BaseType_t woken = pdFALSE;
      if (notify->task && bits)
        {
          xTaskNotifyFromISR(notify->task, bits, eSetBits, &woken);
        }
      if (notify->waiter)
        {
          xTaskNotifyFromISR(notify->waiter, 0, eNoAction, &woken);
        }
      portYIELD_FROM_ISR(woken);
    }
  else
    {
      if (notify->task && bits)
        {
          xTaskNotify(notify->task, bits, eSetBits);
        }
      if (notify->waiter)
        {
          xTaskNotify(notify->waiter, 0, eNoAction);
        }
    }
}

#endif
