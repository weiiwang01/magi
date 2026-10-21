//
// Created by weiiwang01 on 10/18/26.
//

#include "cmd.h"
#include "config.h"
#include "stm32g0xx_hal.h"
#include "stream.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/errno.h>

#define CONFIG_SLOT_COUNT (2)
#define CONFIG_SLOT_SIZE (16 * 1024)
#define CONFIG_HEADER_SIZE (16)
#define CONFIG_DATA_SIZE (CONFIG_SLOT_SIZE - CONFIG_HEADER_SIZE)
#define CONFIG_PROGRAMMING_SIZE (8)
#define CONFIG_VERSION (1)

static SemaphoreHandle_t config_mutex;
static StaticSemaphore_t config_mutex_buffer;

typedef __attribute__((packed)) struct
{
  uint16_t version;
  uint16_t size;
  uint32_t crc;
  uint64_t revision;
  uint8_t config[CONFIG_DATA_SIZE];
} config_slot_t;

static config_slot_t *config_slots
    = (config_slot_t *)(FLASH_BASE + 512 * 1024
                        - CONFIG_SLOT_SIZE * CONFIG_SLOT_COUNT);

typedef struct
{
  uint32_t crc;
} config_crc_t;

typedef struct
{
  config_slot_t *slot;
  uint32_t cursor;
  uint32_t end;
  stream_t stream;
} config_reader_t;

typedef struct
{
  config_slot_t *slot;
  uint32_t cursor;
  uint8_t buffer[CONFIG_PROGRAMMING_SIZE];
  bool closed;
  bool failed;
  config_crc_t crc;
  stream_t stream;
} config_writer_t;

static void
config_crc_init(config_crc_t *crc)
{
  crc->crc = UINT32_MAX;
}

static uint32_t
config_crc_finalize(const config_crc_t *crc)
{
  return crc->crc ^ UINT32_MAX;
}

static uint32_t
config_crc32_combine(uint32_t crc_a, uint32_t crc_b, size_t length_b)
{
  // https://github.com/stbrumme/crc32/blob/master/Crc32.cpp

  // degenerated case
  if (length_b == 0)
    return crc_a;

  /// CRC32 => 32 bits
  const uint32_t crc_bits = 32;

  uint32_t odd[crc_bits];  // odd-power-of-two  zeros operator
  uint32_t even[crc_bits]; // even-power-of-two zeros operator

  // put operator for one zero bit in odd
  odd[0] = 0xEDB88320U; // CRC-32 polynomial
  for (int i = 1; i < (int)crc_bits; i++)
    odd[i] = 1U << (i - 1);

  // put operator for two zero bits in even
  // same as gf2_matrix_square(even, odd);
  for (int i = 0; i < (int)crc_bits; i++)
    {
      uint32_t vec = odd[i];
      even[i] = 0;
      for (int j = 0; vec != 0; j++, vec >>= 1)
        if (vec & 1)
          even[i] ^= odd[j];
    }
  // put operator for four zero bits in odd
  // same as gf2_matrix_square(odd, even);
  for (int i = 0; i < (int)crc_bits; i++)
    {
      uint32_t vec = even[i];
      odd[i] = 0;
      for (int j = 0; vec != 0; j++, vec >>= 1)
        if (vec & 1)
          odd[i] ^= even[j];
    }

  // the following loop becomes much shorter if I keep swapping even and odd
  uint32_t *a = even;
  uint32_t *b = odd;
  // apply secondLength zeros to firstCrc32
  for (; length_b > 0; length_b >>= 1)
    {
      // same as gf2_matrix_square(a, b);
      for (int i = 0; i < (int)crc_bits; i++)
        {
          uint32_t vec = b[i];
          a[i] = 0;
          for (int j = 0; vec != 0; j++, vec >>= 1)
            if (vec & 1)
              a[i] ^= b[j];
        }

      // apply zeros operator for this bit
      if (length_b & 1)
        {
          // same as firstCrc32 = gf2_matrix_times(a, firstCrc32);
          uint32_t sum = 0;
          for (int i = 0; crc_a != 0; i++, crc_a >>= 1)
            if (crc_a & 1)
              sum ^= a[i];
          crc_a = sum;
        }

      // switch even and odd
      uint32_t *t = a;
      a = b;
      b = t;
    }

  // return combined crc
  return crc_a ^ crc_b;
}

static uint32_t
config_crc_feed(config_crc_t *crc, uint8_t data)
{
  // https://github.com/stbrumme/crc32/blob/master/Crc32.cpp
  static const uint32_t crc32_lookup16[16]
      = { 0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
          0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
          0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
          0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C };

  crc->crc = crc32_lookup16[(crc->crc ^ data) & 0x0F] ^ (crc->crc >> 4);
  crc->crc = crc32_lookup16[(crc->crc ^ (data >> 4)) & 0x0F] ^ (crc->crc >> 4);

  return crc->crc;
}

static void
config_crc_header(config_crc_t *crc, const uint8_t *header)
{
  for (uint32_t index = 0; index < CONFIG_HEADER_SIZE; index++)
    {
      bool crc_byte
          = index >= offsetof(config_slot_t, crc)
            && index < offsetof(config_slot_t, crc) + sizeof(uint32_t);
      config_crc_feed(crc, crc_byte ? 0 : header[index]);
    }
}

static int
config_slot_erase(config_slot_t *slot)
{
  uint32_t bank = ((uintptr_t)slot - FLASH_BASE) / FLASH_BANK_SIZE;
  uint32_t page = ((uintptr_t)slot - FLASH_BASE - (bank * FLASH_BANK_SIZE))
                  / FLASH_PAGE_SIZE;
  FLASH_EraseInitTypeDef init;
  init.TypeErase = FLASH_TYPEERASE_PAGES;
  init.Banks = bank == 0 ? FLASH_BANK_1 : FLASH_BANK_2;
  init.Page = page;
  init.NbPages = CONFIG_SLOT_SIZE / FLASH_PAGE_SIZE;
  uint32_t page_error = 0;
  if (HAL_FLASH_Unlock() != HAL_OK)
    {
      return EIO;
    }
  HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&init, &page_error);
  HAL_StatusTypeDef lock_status = HAL_FLASH_Lock();
  if (status != HAL_OK || lock_status != HAL_OK || page_error != UINT32_MAX)
    {
      return EIO;
    }
  return 0;
}

static bool
config_slot_check(config_slot_t *slot)
{
  if (slot->version != CONFIG_VERSION || slot->revision == 0
      || slot->revision == UINT64_MAX)
    {
      return false;
    }
  config_crc_t crc;
  config_crc_init(&crc);
  uint32_t size = slot->size;
  if (size > CONFIG_DATA_SIZE)
    {
      return false;
    }
  config_crc_header(&crc, (const uint8_t *)slot);
  for (uint32_t i = 0; i < size; i++)
    {
      config_crc_feed(&crc, slot->config[i]);
    }
  return config_crc_finalize(&crc) == slot->crc;
}

static stream_status_t
config_reader_exchange(stream_t *stream,
                       uint8_t *read,
                       uint8_t *write,
                       stream_opts_t *opts)
{
  stream_status_t status = 0;
  config_reader_t *config_reader
      = container_of(stream, config_reader_t, stream);
  if (config_reader->slot->size > CONFIG_DATA_SIZE)
    {
      return STREAM_ABORT;
    }
  if (config_reader->cursor >= config_reader->end)
    {
      status |= STREAM_END | STREAM_CLOSED;
      return status;
    }
  if (read == NULL)
    {
      return status;
    }
  *read = config_reader->slot->config[config_reader->cursor];
  config_reader->cursor += 1;
  status |= STREAM_READ_OK;
  return status;
}

static void
config_stream_notify(stream_t *stream, uint32_t read, uint32_t write)
{
}

static void
config_reader_init(config_reader_t *stream, config_slot_t *slot)
{
  stream->slot = slot;
  stream->cursor = 0;
  stream->end = slot->size;
  stream->stream.exchange = config_reader_exchange;
  stream->stream.notify = config_stream_notify;
}

static stream_status_t
config_writer_program(config_writer_t *config_writer, uint32_t offset)
{
  if (HAL_FLASH_Unlock() != HAL_OK)
    {
      config_writer->failed = true;
      return STREAM_ABORT;
    }
  uint64_t data;
  memcpy(&data, config_writer->buffer, sizeof(data));
  HAL_StatusTypeDef status
      = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                          (uintptr_t)config_writer->slot->config + offset,
                          data);
  HAL_StatusTypeDef lock_status = HAL_FLASH_Lock();
  if (status != HAL_OK || lock_status != HAL_OK)
    {
      config_writer->failed = true;
      return STREAM_ABORT;
    }
  return 0;
}

static stream_status_t
config_writer_flush(config_writer_t *config_writer)
{
  if (config_writer->failed)
    {
      return STREAM_ABORT;
    }
  if (config_writer->closed)
    {
      return 0;
    }
  uint32_t buffer_len = config_writer->cursor % CONFIG_PROGRAMMING_SIZE;
  if (buffer_len == 0)
    {
      return 0;
    }
  memset(config_writer->buffer + buffer_len,
         0xFF,
         CONFIG_PROGRAMMING_SIZE - buffer_len);
  stream_status_t status = config_writer_program(
      config_writer, config_writer->cursor - buffer_len);
  if (!stream_error(status))
    {
      config_writer->closed = true;
    }
  return status;
}

static stream_status_t
config_writer_exchange(stream_t *stream,
                       uint8_t *read,
                       uint8_t *write,
                       stream_opts_t *opts)
{
  stream_status_t status = 0;
  config_writer_t *config_writer
      = container_of(stream, config_writer_t, stream);
  if (config_writer->failed)
    {
      return STREAM_ABORT;
    }
  if (config_writer->closed || config_writer->cursor >= CONFIG_DATA_SIZE)
    {
      if (write != NULL)
        {
          config_writer->failed = true;
        }
      status |= STREAM_END | STREAM_CLOSED;
      return status;
    }
  if (write == NULL)
    {
      return status;
    }
  uint32_t buffer_len = config_writer->cursor % CONFIG_PROGRAMMING_SIZE;
  config_writer->buffer[buffer_len] = *write;
  if (buffer_len == CONFIG_PROGRAMMING_SIZE - 1)
    {
      status = config_writer_program(config_writer,
                                     config_writer->cursor - buffer_len);
      if (stream_error(status))
        {
          return status;
        }
    }
  config_writer->cursor += 1;
  config_crc_feed(&config_writer->crc, *write);
  status |= STREAM_WRITE_OK;
  return status;
}

static void
config_writer_init(config_writer_t *config_writer, config_slot_t *slot)
{
  config_writer->slot = slot;
  config_writer->cursor = 0;
  config_writer->closed = false;
  config_writer->failed = false;
  config_crc_init(&config_writer->crc);
  config_writer->stream.exchange = config_writer_exchange;
  config_writer->stream.notify = config_stream_notify;
}

stream_status_t
config_read_vint(stream_t *stream, uint32_t *read)
{
  uint32_t data = 0;
  uint8_t octet = 0;
  stream_status_t status = 0;
  for (uint32_t shift = 0; shift < 32; shift += 7)
    {
      status = stream_getc(stream, &octet);
      if (stream_error(status) || !(status & STREAM_READ_OK))
        {
          return status | STREAM_ABORT;
        }
      if (shift == 28 && (octet & 0xF0) != 0)
        {
          return status | STREAM_ABORT;
        }
      data |= (uint32_t)(octet & 0x7F) << shift;
      if ((octet & 0x80) == 0)
        {
          *read = data;
          return status;
        }
    }
  status |= STREAM_ABORT;
  return status;
}

stream_status_t
config_write_vint(stream_t *stream, uint32_t write)
{
  stream_status_t status = 0;
  while (true)
    {
      uint8_t octet = write & 0x7F;
      write >>= 7;
      bool end = write == 0;
      if (!end)
        {
          octet |= 0x80;
        }
      status = stream_putc(stream, octet);
      if (stream_error(status) || !(status & STREAM_WRITE_OK))
        {
          return status | STREAM_ABORT;
        }
      if (end)
        {
          return status;
        }
    }
}

config_slot_t *
config_get_write_slot()
{
  config_slot_t *write_slot = &config_slots[0];
  for (uint32_t i = 1; i < CONFIG_SLOT_COUNT; i++)
    {
      if (!config_slot_check(write_slot))
        {
          return write_slot;
        }
      config_slot_t *slot = &config_slots[i];
      if (!config_slot_check(slot))
        {
          return slot;
        }
      if (slot->revision <= write_slot->revision)
        {
          write_slot = slot;
        }
    }
  return write_slot;
}

config_slot_t *
config_get_read_slot()
{
  config_slot_t *read_slot = NULL;
  for (uint32_t i = 0; i < CONFIG_SLOT_COUNT; i++)
    {
      config_slot_t *slot = &config_slots[i];
      if (!config_slot_check(slot))
        {
          continue;
        }
      if (read_slot == NULL || slot->revision > read_slot->revision)
        {
          read_slot = slot;
        }
    }
  return read_slot;
}

int
config_load_cmd(config_reader_t *reader, int *exit)
{
  uint32_t symbols = 0;
  stream_status_t status = config_read_vint(&reader->stream, &symbols);
  if (stream_error(status))
    {
      return EIO;
    }
  if (symbols < 2 || symbols > CMD_LEVEL_MAX + 1)
    {
      return EIO;
    }
  cmd_node_t *node = NULL;
  uint8_t symbol[CMD_SYMBOL_MAX];
  bool unknown_cmd = false;
  for (uint32_t index = 0; index < symbols - 1; index++)
    {
      uint32_t symbol_len = 0;
      status = config_read_vint(&reader->stream, &symbol_len);
      if (stream_error(status))
        {
          return EIO;
        }
      if (symbol_len == 0 || symbol_len > CMD_SYMBOL_MAX)
        {
          return EIO;
        }
      uint32_t symbol_read = 0;
      status = stream_gets(&reader->stream, symbol, symbol_len, &symbol_read);
      if (stream_error(status) || symbol_read != symbol_len)
        {
          return EIO;
        }
      if (!unknown_cmd)
        {
          node = cmd_search_node(node, symbol, symbol_len);
          if (node == NULL)
            {
              unknown_cmd = true;
            }
        }
    }
  uint32_t argument_start = reader->cursor;
  uint32_t argument_len = 0;
  status = config_read_vint(&reader->stream, &argument_len);
  if (stream_error(status) || argument_len > reader->end - reader->cursor)
    {
      return EIO;
    }
  uint32_t argument_end = reader->cursor + argument_len;
  *exit = 0;
  if (unknown_cmd || node == NULL || node->load == NULL)
    {
      reader->cursor = argument_end;
      return 0;
    }
  uint32_t end = reader->end;
  reader->cursor = argument_start;
  reader->end = argument_end;
  *exit = node->load(node, &reader->stream);
  reader->end = end;
  if (*exit != 0 || reader->cursor != argument_end)
    {
      return EIO;
    }
  return 0;
}

int
config_load()
{
  config_slot_t *slot = config_get_read_slot();
  if (slot == NULL)
    {
      return 0;
    }
  config_reader_t reader;
  config_reader_init(&reader, slot);
  while (true)
    {
      if (reader.cursor >= slot->size)
        {
          return 0;
        }
      int exit = 0;
      int status = config_load_cmd(&reader, &exit);
      if (status != 0 || exit != 0)
        {
          return EIO;
        }
    }
}

int
config_save_cmd(config_writer_t *writer,
                cmd_node_t *path[],
                uint32_t level,
                int *exit)
{
  stream_status_t status = config_write_vint(&writer->stream, level + 1);
  if (stream_error(status))
    {
      return EIO;
    }
  cmd_node_t *node = path[0];
  for (uint32_t index = 0; index < level; index++)
    {
      node = path[index];
      size_t symbol_len = strlen(node->symbol);
      if (symbol_len == 0 || symbol_len > CMD_SYMBOL_MAX)
        {
          return EIO;
        }
      status = config_write_vint(&writer->stream, symbol_len);
      if (stream_error(status))
        {
          return EIO;
        }
      status = stream_puts(&writer->stream, node->symbol);
      if (stream_error(status))
        {
          return EIO;
        }
    }
  *exit = node->save(node, &writer->stream);
  if (*exit != 0 || writer->failed)
    {
      return EIO;
    }
  return 0;
}

static int
config_commit(config_slot_t *slot,
              uint64_t revision,
              uint16_t size,
              config_crc_t crc)
{
  uint64_t header[2] = { CONFIG_VERSION | ((uint64_t)size << 16), revision };
  config_crc_t header_crc;
  config_crc_init(&header_crc);
  config_crc_header(&header_crc, (const uint8_t *)header);
  uint32_t combined_crc = config_crc32_combine(
      config_crc_finalize(&header_crc), config_crc_finalize(&crc), size);
  header[0] |= (uint64_t)combined_crc << 32;
  if (HAL_FLASH_Unlock() != HAL_OK)
    {
      return EIO;
    }
  HAL_StatusTypeDef status = HAL_FLASH_Program(
      FLASH_TYPEPROGRAM_DOUBLEWORD, (uintptr_t)&slot->revision, revision);
  if (status == HAL_OK)
    {
      status = HAL_FLASH_Program(
          FLASH_TYPEPROGRAM_DOUBLEWORD, (uintptr_t)slot, header[0]);
    }
  HAL_StatusTypeDef lock_status = HAL_FLASH_Lock();
  if (status != HAL_OK || lock_status != HAL_OK || !config_slot_check(slot))
    {
      return EIO;
    }
  return 0;
}

static int
config_save_locked()
{
  config_slot_t *previous = config_get_read_slot();
  if (previous != NULL && previous->revision == UINT64_MAX - 1)
    {
      return EIO;
    }
  uint64_t revision = previous == NULL ? 1 : previous->revision + 1;
  config_slot_t *slot = config_get_write_slot();
  if (config_slot_erase(slot) != 0)
    {
      return EIO;
    }
  config_writer_t writer;
  config_writer_init(&writer, slot);

  cmd_node_t *path[CMD_LEVEL_MAX] = { 0 };
  path[0] = cmd_root;
  uint32_t level = 1;
  while (level > 0)
    {
      cmd_node_t *node = path[level - 1];
      if (node->save != NULL && node->dirty)
        {
          int exit = 0;
          int status = config_save_cmd(&writer, path, level, &exit);
          if (status != 0 || exit != 0)
            {
              return EIO;
            }
        }
      if (node->child != NULL)
        {
          if (level == CMD_LEVEL_MAX)
            {
              return EIO;
            }
          path[level] = node->child;
          level += 1;
          continue;
        }
      while (level > 0 && !path[level - 1]->next)
        {
          level -= 1;
        }
      if (level > 0)
        {
          path[level - 1] = path[level - 1]->next;
        }
    }
  if (stream_error(config_writer_flush(&writer)))
    {
      return EIO;
    }
  return config_commit(slot, revision, writer.cursor, writer.crc);
}

static void
config_lock()
{
  taskENTER_CRITICAL();
  if (config_mutex == NULL)
    {
      config_mutex = xSemaphoreCreateMutexStatic(&config_mutex_buffer);
    }
  taskEXIT_CRITICAL();
  xSemaphoreTake(config_mutex, portMAX_DELAY);
}

static void
config_unlock()
{
  xSemaphoreGive(config_mutex);
}

int
config_save()
{
  config_lock();
  int status = config_save_locked();
  config_unlock();
  return status;
}

static int
config_reset_locked()
{
  for (int i = 0; i < CONFIG_SLOT_COUNT; i++)
    {
      int status = config_slot_erase(&config_slots[i]);
      if (status != 0)
        {
          return status;
        }
    }
  return 0;
}

int
config_reset()
{
  config_lock();
  int status = config_reset_locked();
  config_unlock();
  return status;
}
