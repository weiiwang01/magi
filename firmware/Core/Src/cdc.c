//
// Created by weiiwang01 on 9/10/26.
//

#include "FreeRTOS.h"
#include "task.h"

#include "cdc.h"
#include "cmd.h"
#include "fifo.h"
#include "stream.h"
#include "stream_notify.h"
#include "usbd_cdc.h"

#define CDC_BUFFER_SIZE 64U
#define CDC_TASK_STACK_SIZE 768U

typedef struct
{
  TaskHandle_t task;
  stream_t stream;
  stream_notify_t notify;
  USBD_HandleTypeDef *device;
  uint8_t tx_buffer[CDC_BUFFER_SIZE];
  fifo_t tx_fifo;
  uint8_t rx_buffer[CDC_DATA_FS_MAX_PACKET_SIZE];
  uint32_t rx_offset;
  uint32_t rx_count;
} cdc_shell_t;

static StaticTask_t cdc_task_buffer;
static StackType_t cdc_task_stack[CDC_TASK_STACK_SIZE];

static stream_status_t cdc_shell_exchange(stream_t *stream,
                                          uint8_t *read,
                                          uint8_t *write,
                                          stream_opts_t *opts);
static void cdc_shell_notify(stream_t *stream, uint32_t read, uint32_t write);

static cdc_shell_t cdc_shell = {
  .stream = { .exchange = cdc_shell_exchange, .notify = cdc_shell_notify },
};

static void
cdc_notify(bool read, bool write)
{
  stream_notify_send(&cdc_shell.notify, read, write);
}

static bool
cdc_ready()
{
  return cdc_shell.device != NULL
         && cdc_shell.device->dev_state == USBD_STATE_CONFIGURED
         && cdc_shell.device->pClassData != NULL;
}

static void
cdc_flush()
{
  if (!cdc_ready())
    {
      return;
    }

  USBD_CDC_HandleTypeDef *cdc = cdc_shell.device->pClassData;
  if (cdc->TxState != 0)
    {
      return;
    }

  uint8_t *data;
  uint32_t length = fifo_dma_read(&cdc_shell.tx_fifo, &data, CDC_BUFFER_SIZE);
  if (length == 0)
    {
      return;
    }
  USBD_CDC_SetTxBuffer(cdc_shell.device, data, length);
  if (USBD_CDC_TransmitPacket(cdc_shell.device) != USBD_OK)
    {
      fifo_dma_read_done(&cdc_shell.tx_fifo, 0);
    }
}

uint8_t
cdc_usb_init(USBD_HandleTypeDef *device)
{
  cdc_shell.device = device;
  fifo_init(
      &cdc_shell.tx_fifo, cdc_shell.tx_buffer, sizeof(cdc_shell.tx_buffer));
  cdc_shell.rx_offset = 0;
  cdc_shell.rx_count = 0;
  USBD_CDC_SetTxBuffer(device, cdc_shell.tx_buffer, 0);
  USBD_CDC_SetRxBuffer(device, cdc_shell.rx_buffer);
  cdc_notify(true, true);
  return USBD_OK;
}

void
cdc_usb_deinit()
{
  cdc_shell.device = NULL;
  fifo_init(
      &cdc_shell.tx_fifo, cdc_shell.tx_buffer, sizeof(cdc_shell.tx_buffer));
  cdc_shell.rx_count = 0;
  cdc_notify(true, true);
}

void
cdc_usb_receive(uint8_t *data, uint32_t length)
{
  cdc_shell.rx_offset = 0;
  cdc_shell.rx_count = length;
  if (length == 0)
    {
      USBD_CDC_ReceivePacket(cdc_shell.device);
    }
  else
    {
      cdc_notify(true, false);
    }
}

void
cdc_usb_transmit_complete(uint32_t length)
{
  fifo_dma_read_done(&cdc_shell.tx_fifo, length);
  cdc_flush();
  cdc_notify(false, true);
}

static uint8_t
cdc_transmit(const uint8_t *data, uint16_t length)
{
  uint8_t result = USBD_FAIL;

  taskENTER_CRITICAL();
  if (cdc_ready())
    {
      result = USBD_BUSY;
      if (length
          <= fifo_capacity(&cdc_shell.tx_fifo) - fifo_size(&cdc_shell.tx_fifo))
        {
          for (uint32_t i = 0; i < length; ++i)
            {
              fifo_putc(&cdc_shell.tx_fifo, data[i]);
            }
          cdc_flush();
          result = USBD_OK;
        }
    }
  taskEXIT_CRITICAL();
  return result;
}

static void
cdc_shell_task(void *argument)
{
  cmd_shell(&cdc_shell.stream);
}

void
cdc_init()
{
  if (cdc_shell.task == NULL)
    {
      cdc_shell.task = xTaskCreateStatic(cdc_shell_task,
                                         "cdc_shell",
                                         CDC_TASK_STACK_SIZE,
                                         NULL,
                                         tskIDLE_PRIORITY + 1,
                                         cdc_task_stack,
                                         &cdc_task_buffer);
    }
}

static stream_status_t
cdc_shell_exchange(stream_t *stream,
                   uint8_t *read,
                   uint8_t *write,
                   stream_opts_t *opts)
{
  if (!read && !write)
    {
      return 0;
    }

  for (;;)
    {
      taskENTER_CRITICAL();
      stream_status_t status = 0;
      if (!cdc_ready() && opts && opts->nonblocking)
        {
          taskEXIT_CRITICAL();
          return STREAM_CLOSED;
        }
      if (read)
        {
          if (cdc_shell.rx_count != 0)
            {
              *read = cdc_shell.rx_buffer[cdc_shell.rx_offset++];
              cdc_shell.rx_count--;
              if (cdc_shell.rx_count == 0)
                {
                  USBD_CDC_ReceivePacket(cdc_shell.device);
                }
              status |= STREAM_READ_OK;
            }
        }

      if (write && cdc_transmit(write, 1) == USBD_OK)
        {
          status |= STREAM_WRITE_OK;
        }

      if (status != 0 || (opts != NULL && opts->nonblocking))
        {
          taskEXIT_CRITICAL();
          return status;
        }
      cdc_shell.notify.waiter = xTaskGetCurrentTaskHandle();
      taskEXIT_CRITICAL();
      xTaskNotifyWait(0, UINT32_MAX, NULL, portMAX_DELAY);
      taskENTER_CRITICAL();
      cdc_shell.notify.waiter = NULL;
      taskEXIT_CRITICAL();
    }
}

static void
cdc_shell_notify(stream_t *stream, uint32_t read, uint32_t write)
{
  taskENTER_CRITICAL();
  stream_notify_set(&cdc_shell.notify, read, write);
  bool closed = !cdc_ready();
  cdc_notify(closed || cdc_shell.rx_count != 0,
             closed
                 || fifo_size(&cdc_shell.tx_fifo)
                        < fifo_capacity(&cdc_shell.tx_fifo));
  taskEXIT_CRITICAL();
}
