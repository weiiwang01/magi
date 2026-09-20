//
// Created by weiiwang01 on 9/10/26.
//

#ifndef STREAM_H
#define STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "container_of.h"
#include "tiny_printf.h"

typedef struct stream_t stream_t;

typedef enum
{
  STREAM_READ_OK = 1 << 0,
  STREAM_WRITE_OK = 1 << 1,
  STREAM_END = 1 << 2,
  STREAM_CLOSED = 1 << 3,
  STREAM_ABORT = 1 << 4,
} stream_status_t;

typedef struct
{
  bool nonblocking;
} stream_opts_t;

static void
stream_opts_init(stream_opts_t *opts)
{
  *opts = (stream_opts_t){ .nonblocking = false };
}

struct stream_t
{
  stream_status_t (*exchange)(stream_t *stream,
                              uint8_t *read,
                              uint8_t *write,
                              stream_opts_t *opts);
  void (*notify)(stream_t *stream, uint32_t read, uint32_t write);
};

typedef struct
{
  stream_t *stream;
  stream_status_t result;
  struct tiny_printf_info info;
} stream_tiny_printf_t;

static bool
stream_error(stream_status_t result)
{
  return (~(STREAM_WRITE_OK | STREAM_READ_OK) & result) > 0;
};

static void
stream_tiny_printf_putc(struct tiny_printf_info *info, char c)
{
  stream_tiny_printf_t *context
      = container_of(info, stream_tiny_printf_t, info);
  stream_t *stream = context->stream;
  uint8_t data = (uint8_t)c;
  context->result = 0;
  while (!stream_error(context->result))
    {
      context->result = stream->exchange(stream, NULL, &data, NULL);
      if (context->result & STREAM_WRITE_OK)
        {
          return;
        }
    }
}

static stream_status_t
stream_putc(stream_t *stream, uint8_t c)
{
  return stream->exchange(stream, NULL, &c, NULL);
}

static stream_status_t
stream_puts(stream_t *stream, const char *s)
{
  stream_status_t result = 0;
  while (*s)
    {
      result = stream_putc(stream, *s);
      s += 1;
    }
  return result;
}

static stream_status_t
stream_getc(stream_t *stream, uint8_t *c)
{
  return stream->exchange(stream, c, NULL, NULL);
}

static stream_status_t
stream_gets(stream_t *stream, uint8_t *s, uint32_t n, uint32_t *got)
{
  stream_status_t result = 0;
  for (uint32_t i = 0; i < n; i++)
    {
      result = stream_getc(stream, s);
      if (!(result & STREAM_READ_OK))
        {
          return result;
        }
      s += 1;
      *got = i + 1;
    }
  return result;
}

static stream_status_t
stream_drain(stream_t *stream)
{
  stream_status_t result = 0;
  uint8_t c = 0;
  while (true)
    {
      result = stream_getc(stream, &c);
      if (stream_error(result))
        {
          return result;
        }
    }
}

static stream_status_t
stream_vprintf(stream_t *stream, const char *format, va_list args)
{
  stream_tiny_printf_t context = {
    .stream = stream,
    .result = 0,
    .info = { .putc = stream_tiny_printf_putc },
  };
  tiny_vprintf(&context.info, format, args);
  return context.result;
}

__attribute__((format(printf, 2, 3))) static stream_status_t
stream_printf(stream_t *stream, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  stream_status_t result = stream_vprintf(stream, format, args);
  va_end(args);
  return result;
}

#endif // STREAM_H
