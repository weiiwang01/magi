//
// Created by weiiwang01 on 9/8/26.
//

#ifndef FIFO_H
#define FIFO_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  uint8_t *data;
  uint32_t capacity;
  uint32_t mask;
  uint32_t read;
  uint32_t write;
  uint32_t dma_read;
  uint32_t dma_write;
} fifo_t;

static int
fifo_init(fifo_t *fifo, uint8_t *data, uint32_t capacity)
{
  *fifo = (const fifo_t){ 0 };
  if (!capacity || (capacity & (capacity - 1)) != 0)
    {
      return -1;
    }
  fifo->data = data;
  fifo->capacity = capacity;
  fifo->mask = capacity - 1;
  return 0;
}

static uint32_t
fifo_capacity(fifo_t *fifo)
{
  return fifo->capacity;
}

static uint32_t
fifo_size(fifo_t *fifo)
{
  return fifo->write - fifo->read;
}

static uint32_t
fifo_read(fifo_t *fifo, uint8_t *data, uint32_t n)
{
  if (fifo->dma_read)
    {
      return 0;
    }
  uint32_t size = fifo_size(fifo);
  n = size > n ? n : size;
  for (uint32_t i = 0; i < n; i++)
    {
      data[i] = *(fifo->data + (fifo->read & fifo->mask));
      fifo->read += 1;
    }
  return n;
}

static uint32_t
fifo_write(fifo_t *fifo, uint8_t *data, uint32_t n)
{
  if (fifo->dma_write)
    {
      return 0;
    }
  uint32_t free = fifo->capacity - fifo_size(fifo);
  n = free > n ? n : free;
  for (uint32_t i = 0; i < n; i++)
    {
      *(fifo->data + (fifo->write & fifo->mask)) = data[i];
      fifo->write += 1;
    }
  return n;
}

static uint32_t
fifo_dma_read(fifo_t *fifo, uint8_t **data, uint32_t n)
{
  if (fifo->dma_read)
    {
      return 0;
    }
  uint32_t size = fifo_size(fifo);
  uint32_t continuous = fifo->capacity - (fifo->read & fifo->mask);
  n = size > n ? n : size;
  n = continuous > n ? n : continuous;
  if (n == 0)
    {
      return 0;
    }
  fifo->dma_read = n;
  *data = fifo->data + (fifo->read & fifo->mask);
  return n;
}

static void
fifo_dma_read_done(fifo_t *fifo, uint32_t n)
{
  if (!fifo->dma_read)
    {
      return;
    }
  uint32_t advance = n <= fifo->dma_read ? n : fifo->dma_read;
  fifo->read += advance;
  fifo->dma_read = 0;
}

static uint32_t
fifo_dma_write(fifo_t *fifo, uint8_t **data, uint32_t n)
{
  if (fifo->dma_write)
    {
      return 0;
    }
  uint32_t free = fifo->capacity - fifo_size(fifo);
  uint32_t continuous = fifo->capacity - (fifo->write & fifo->mask);
  n = free > n ? n : free;
  n = continuous > n ? n : continuous;
  if (n == 0)
    {
      return 0;
    }
  fifo->dma_write = n;
  *data = fifo->data + (fifo->write & fifo->mask);
  return n;
}

static void
fifo_dma_write_done(fifo_t *fifo, uint32_t n)
{
  if (!fifo->dma_write)
    {
      return;
    }
  uint32_t advance = n <= fifo->dma_write ? n : fifo->dma_write;
  fifo->write += advance;
  fifo->dma_write = 0;
}

static bool
fifo_getc(fifo_t *fifo, uint8_t *data)
{
  return fifo_read(fifo, data, 1) == 1;
}

static bool
fifo_putc(fifo_t *fifo, uint8_t data)
{
  return fifo_write(fifo, &data, 1) == 1;
}

#endif // FIFO_H
