#include <stddef.h>

#include "cmd.h"
#include "config.h"

#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "adc.h"
#include "container_of.h"
#include "console.h"
#include "crypto.h"
#include "cryptoauthlib.h"
#include "header.h"
#include "network.h"
#include "ssh_server.h"
#include "tcpm.h"
#include "unit.h"

#define CMD_LINE_MAX 512

#define CMD_SYMBOL_GET "get"
#define CMD_SYMBOL_SET "set"

typedef struct
{
  cmd_node_t node;
  unit_t **unit;
} cmd_unit_node_t;

typedef struct
{
  cmd_node_t node;
  usb_t usb;
} cmd_usb_node_t;

static int
cmd_read_value(stream_t *stream, char *value, uint32_t size)
{
  uint32_t length = 0;
  stream_status_t result
      = stream_gets(stream, (uint8_t *)value, size - 1U, &length);
  if (result & STREAM_ABORT)
    {
      return CMD_EXIT_INTERRUPT;
    }
  if (stream_error(result & ~STREAM_END))
    {
      return CMD_EXIT_IOERR;
    }
  if (!(result & STREAM_END) || length == 0
      || memchr(value, '\0', length) != NULL)
    {
      return stream_error(stream_puts(stream, "Invalid value\r\n"))
                 ? CMD_EXIT_IOERR
                 : CMD_EXIT_MISUSE;
    }
  value[length] = '\0';
  return CMD_EXIT_OK;
}

static int
cmd_read_int(stream_t *stream, int *value)
{
  char input[13];
  int result = cmd_read_value(stream, input, sizeof(input));
  if (result != CMD_EXIT_OK)
    {
      return result;
    }
  const char *digits = input[0] == '-' ? input + 1 : input;
  errno = 0;
  long number = strtol(input, NULL, 10);
  if (errno == ERANGE || number < INT_MIN || number > INT_MAX
      || *digits == '\0' || digits[strspn(digits, "0123456789")] != '\0')
    {
      return stream_error(stream_puts(stream, "expected an integer\r\n"))
                 ? CMD_EXIT_IOERR
                 : CMD_EXIT_MISUSE;
    }
  *value = (int)number;
  return CMD_EXIT_OK;
}

static int
cmd_read_bool(stream_t *stream, bool *value)
{
  char input[7];
  int result = cmd_read_value(stream, input, sizeof(input));
  if (result != CMD_EXIT_OK)
    {
      return result;
    }
  if (strcmp(input, "true") != 0 && strcmp(input, "false") != 0)
    {
      return stream_error(stream_puts(stream, "Expected true or false\r\n"))
                 ? CMD_EXIT_IOERR
                 : CMD_EXIT_MISUSE;
    }
  *value = strcmp(input, "true") == 0;
  return CMD_EXIT_OK;
}

static int
cmd_unit_type_encode(cmd_node_t *cmd, stream_t *stream)
{
  unit_t *unit = *container_of(cmd, cmd_unit_node_t, node)->unit;
  return stream_error(stream_printf(
             stream, "%s\r\n", unit_type_names[unit_get_type(unit)]))
             ? CMD_EXIT_IOERR
             : CMD_EXIT_OK;
}

static int
cmd_unit_type_set(cmd_node_t *cmd, const char *name)
{
  for (size_t type = 0;
       type < sizeof(unit_type_names) / sizeof(unit_type_names[0]);
       ++type)
    {
      if (strcmp(name, unit_type_names[type]) == 0)
        {
          unit_t *unit = *container_of(cmd, cmd_unit_node_t, node)->unit;
          unit_set_type(unit, (unit_type_t)type);
          cmd->dirty = type != UNIT_TYPE_CM5;
          return CMD_EXIT_OK;
        }
    }
  return CMD_EXIT_MISUSE;
}

static int
cmd_unit_type_decode(cmd_node_t *cmd, stream_t *stream)
{
  char input[5];
  int result = cmd_read_value(stream, input, sizeof(input));
  if (result != CMD_EXIT_OK)
    {
      return result;
    }
  if (cmd_unit_type_set(cmd, input) == CMD_EXIT_OK)
    {
      return CMD_EXIT_OK;
    }
  return stream_error(stream_puts(stream, "Expect cm5 or cm4\r\n"))
             ? CMD_EXIT_IOERR
             : CMD_EXIT_MISUSE;
}

static int
cmd_unit_type_save(cmd_node_t *cmd, stream_t *stream)
{
  unit_t *unit = *container_of(cmd, cmd_unit_node_t, node)->unit;
  const char *name = unit_type_names[unit_get_type(unit)];
  stream_status_t status = config_write_vint(stream, strlen(name));
  if (stream_error(status) || !(status & STREAM_WRITE_OK))
    {
      return CMD_EXIT_IOERR;
    }
  return stream_error(stream_puts(stream, name)) ? CMD_EXIT_IOERR
                                                 : CMD_EXIT_OK;
}

static int
cmd_unit_type_load(cmd_node_t *cmd, stream_t *stream)
{
  char name[5];
  uint32_t length;
  stream_status_t status = config_read_vint(stream, &length);
  if (stream_error(status) || !(status & STREAM_READ_OK) || length == 0U
      || length >= sizeof(name))
    {
      return CMD_EXIT_IOERR;
    }
  uint32_t count = 0;
  status = stream_gets(stream, (uint8_t *)name, length, &count);
  if (stream_error(status) || count != length
      || memchr(name, '\0', length) != NULL)
    {
      return CMD_EXIT_IOERR;
    }
  name[length] = '\0';
  if (cmd_unit_type_set(cmd, name) != CMD_EXIT_OK)
    {
      return CMD_EXIT_IOERR;
    }
  return CMD_EXIT_OK;
}

static int
cmd_unit_power_encode(cmd_node_t *cmd, stream_t *stream)
{
  unit_t *unit = *container_of(cmd, cmd_unit_node_t, node)->unit;
  return stream_error(stream_printf(
             stream, "%s\r\n", unit_get_power(unit) ? "true" : "false"))
             ? CMD_EXIT_IOERR
             : CMD_EXIT_OK;
}

static int
cmd_unit_power_decode(cmd_node_t *cmd, stream_t *stream)
{
  bool enable;
  int result = cmd_read_bool(stream, &enable);
  if (result != CMD_EXIT_OK)
    {
      return result;
    }
  unit_t *unit = *container_of(cmd, cmd_unit_node_t, node)->unit;
  unit_set_power(unit, enable);
  return CMD_EXIT_OK;
}

static int
cmd_unit_rpiboot_encode(cmd_node_t *cmd, stream_t *stream)
{
  unit_t *unit = *container_of(cmd, cmd_unit_node_t, node)->unit;
  return stream_error(stream_printf(
             stream, "%s\r\n", unit_get_rpiboot(unit) ? "true" : "false"))
             ? CMD_EXIT_IOERR
             : CMD_EXIT_OK;
}

static int
cmd_unit_rpiboot_decode(cmd_node_t *cmd, stream_t *stream)
{
  bool enable;
  int result = cmd_read_bool(stream, &enable);
  if (result != CMD_EXIT_OK)
    {
      return result;
    }
  unit_t *unit = *container_of(cmd, cmd_unit_node_t, node)->unit;
  unit_set_rpiboot(unit, enable);
  return CMD_EXIT_OK;
}

static int
cmd_unit_power_button_press_encode(cmd_node_t *cmd, stream_t *stream)
{
  unit_t *unit = *container_of(cmd, cmd_unit_node_t, node)->unit;
  return stream_error(
             stream_printf(stream,
                           "%lu\r\n",
                           (unsigned long)unit_get_power_button_press(unit)))
             ? CMD_EXIT_IOERR
             : CMD_EXIT_OK;
}

static int
cmd_unit_power_button_press_decode(cmd_node_t *cmd, stream_t *stream)
{
  int milliseconds;
  int result = cmd_read_int(stream, &milliseconds);
  if (result != CMD_EXIT_OK)
    {
      return result;
    }
  if (milliseconds < 0 || milliseconds > 60000)
    {
      return stream_error(stream_puts(
                 stream, "Expected milliseconds from 0 to 60000\r\n"))
                 ? CMD_EXIT_IOERR
                 : CMD_EXIT_MISUSE;
    }
  unit_t *unit = *container_of(cmd, cmd_unit_node_t, node)->unit;
  unit_set_power_button_press(unit, (uint32_t)milliseconds);
  return CMD_EXIT_OK;
}

static int
cmd_console_exec(console_t *console, unsigned int unit, stream_t *stream)
{
  console_tap_t tap;
  console_tap_init(&tap);
  if (!console_is_enabled(console) || console_tap_attach(&tap, console) != 0)
    {
      return CMD_EXIT_IOERR;
    }
  stream_status_t greeting
      = stream_printf(stream,
                      "Attached to unit %u console, press ctrl-b + q to exit "
                      "and ctrl-b + ctrl-b to send ctrl-b.\r\n",
                      unit);
  if (stream_error(greeting))
    {
      console_tap_detach(&tap);
      return greeting & STREAM_ABORT ? CMD_EXIT_INTERRUPT : CMD_EXIT_IOERR;
    }
  stream_t *serial = &tap.stream;
  stream_opts_t opts = { .nonblocking = true };
  uint8_t to_console = 0, to_command = 0;
  bool command_to_serial = false, serial_to_command = false, escape = false;
  int exit_code = CMD_EXIT_OK;
  const uint32_t command_read = 1U << 0, command_write = 1U << 1;
  const uint32_t console_read = 1U << 2, console_write = 1U << 3;
  uint32_t command_notifications = UINT32_MAX,
           console_notifications = UINT32_MAX;

  for (;;)
    {
      uint32_t command_interest = (!command_to_serial ? command_read : 0)
                                  | (serial_to_command ? command_write : 0);
      uint32_t console_interest = (!serial_to_command ? console_read : 0)
                                  | (command_to_serial ? console_write : 0);
      if (command_interest != command_notifications)
        {
          stream->notify(stream,
                         command_interest & command_read,
                         command_interest & command_write);
          command_notifications = command_interest;
        }
      if (console_interest != console_notifications)
        {
          serial->notify(serial,
                         console_interest & console_read,
                         console_interest & console_write);
          console_notifications = console_interest;
        }
      uint8_t input = 0;
      stream_status_t result
          = stream->exchange(stream,
                             !command_to_serial ? &input : NULL,
                             serial_to_command ? &to_command : NULL,
                             &opts);
      bool progress = (result & (STREAM_READ_OK | STREAM_WRITE_OK)) != 0;
      if (stream_error(result))
        {
          exit_code
              = result & STREAM_ABORT ? CMD_EXIT_INTERRUPT : CMD_EXIT_IOERR;
          break;
        }
      if (result & STREAM_WRITE_OK)
        {
          serial_to_command = false;
        }
      if (result & STREAM_READ_OK)
        {
          if (escape)
            {
              escape = false;
              if (input == 'q')
                {
                  break;
                }
              if (input == 0x02)
                {
                  to_console = input;
                  command_to_serial = true;
                }
            }
          else if (input == 0x02)
            {
              escape = true;
            }
          else
            {
              to_console = input;
              command_to_serial = true;
            }
        }
      result = serial->exchange(serial,
                                !serial_to_command ? &to_command : NULL,
                                command_to_serial ? &to_console : NULL,
                                &opts);
      if (stream_error(result))
        {
          exit_code
              = result & STREAM_ABORT ? CMD_EXIT_INTERRUPT : CMD_EXIT_IOERR;
          break;
        }
      if (result & STREAM_READ_OK)
        {
          serial_to_command = true;
        }
      if (result & STREAM_WRITE_OK)
        {
          command_to_serial = false;
        }
      progress |= (result & (STREAM_READ_OK | STREAM_WRITE_OK)) != 0;
      if (!progress)
        {
          xTaskNotifyWait(0,
                          command_read | command_write | console_read
                              | console_write,
                          NULL,
                          portMAX_DELAY);
        }
    }
  stream->notify(stream, 0, 0);
  serial->notify(serial, 0, 0);
  console_tap_detach(&tap);
  return exit_code;
}

static int
cmd_unit1_console_exec(cmd_node_t *cmd, stream_t *stream)
{
  return cmd_console_exec(console_unit_1, 1, stream);
}

static int
cmd_unit2_console_exec(cmd_node_t *cmd, stream_t *stream)
{
  return cmd_console_exec(console_unit_2, 2, stream);
}

static int
cmd_unit3_console_exec(cmd_node_t *cmd, stream_t *stream)
{
  return cmd_console_exec(console_unit_3, 3, stream);
}

static const char *
cmd_crypto_lock_state(uint8_t value)
{
  return value == ATCA_LOCKED     ? "locked"
         : value == ATCA_UNLOCKED ? "unlocked"
                                  : "unknown";
}

static int
cmd_magi_crypto_status_encode(cmd_node_t *cmd, stream_t *stream)
{
  atecc608_config_t config;
  uint8_t revision[4];
  uint8_t serial[9];

  ATCA_STATUS result = crypto_lock();
  if (result != ATCA_SUCCESS)
    {
      return stream_error(
                 stream_printf(stream,
                               "Crypto module unavailable: %d (0x%02x)\r\n",
                               result,
                               (unsigned int)(uint8_t)result))
                 ? CMD_EXIT_IOERR
                 : 1;
    }
  result = atcab_info(revision);
  if (result == ATCA_SUCCESS)
    {
      result = atcab_read_config_zone((uint8_t *)&config);
    }
  ATCA_STATUS sleep_result = atcab_wakeup();
  if (sleep_result == ATCA_SUCCESS)
    {
      sleep_result = atcab_sleep();
    }
  crypto_unlock();
  if (result == ATCA_SUCCESS)
    {
      result = sleep_result;
    }
  if (result != ATCA_SUCCESS)
    {
      return stream_error(
                 stream_printf(stream,
                               "ATECC608B status failed: %d (0x%02x)\r\n",
                               result,
                               (unsigned int)(uint8_t)result))
                 ? CMD_EXIT_IOERR
                 : 1;
    }
  memcpy(serial, &config.SN03, 4);
  memcpy(serial + 4, &config.SN47, 4);
  serial[8] = config.SN8;
  if (stream_error(stream_printf(
          stream,
          "Revision: %02x%02x%02x%02x\r\n"
          "Serial number: %02x%02x%02x%02x%02x%02x%02x%02x%02x\r\n"
          "AES enable: 0x%02x\r\nConfig zone: %s (0x%02x)\r\n"
          "Data/OTP zone: %s (0x%02x)\r\nSlotLocked: 0x%04x\r\n"
          "Slot  SlotConfig  KeyConfig  Slot lock\r\n",
          revision[0],
          revision[1],
          revision[2],
          revision[3],
          serial[0],
          serial[1],
          serial[2],
          serial[3],
          serial[4],
          serial[5],
          serial[6],
          serial[7],
          serial[8],
          config.AES_Enable,
          cmd_crypto_lock_state(config.LockConfig),
          config.LockConfig,
          cmd_crypto_lock_state(config.LockValue),
          config.LockValue,
          config.SlotLocked)))
    {
      return CMD_EXIT_IOERR;
    }
  for (unsigned int slot = 0;
       slot < sizeof(config.SlotConfig) / sizeof(config.SlotConfig[0]);
       ++slot)
    {
      if (stream_error(stream_printf(stream,
                                     "%4u  0x%04x      0x%04x     %s\r\n",
                                     slot,
                                     config.SlotConfig[slot],
                                     config.KeyConfig[slot],
                                     (config.SlotLocked & (1U << slot)) != 0
                                         ? "unlocked"
                                         : "locked")))
        {
          return CMD_EXIT_IOERR;
        }
    }
  return CMD_EXIT_OK;
}

static int
cmd_magi_crypto_init_exec(cmd_node_t *cmd, stream_t *stream)
{
  int result = crypto_lock();
  if (result != ATCA_SUCCESS)
    {
      goto report;
    }
  atecc608_config_t config;
  uint8_t protection_key[ATCA_KEY_SIZE] = { 0 };
  result = atcab_read_config_zone((uint8_t *)&config);
  if (result == ATCA_SUCCESS && config.LockConfig == ATCA_LOCKED
      && config.LockValue == ATCA_LOCKED)
    {
      crypto_unlock();
      return stream_error(
                 stream_puts(stream, "ATECC608B is already initialized\r\n"))
                 ? CMD_EXIT_IOERR
                 : CMD_EXIT_OK;
    }
  if (result == ATCA_SUCCESS
      && (config.LockConfig != ATCA_UNLOCKED
          || config.LockValue != ATCA_UNLOCKED))
    {
      result = ATCA_BAD_PARAM;
    }
  if (result == ATCA_SUCCESS)
    {
      for (unsigned int slot = 0; slot < 16; ++slot)
        {
          if (slot < 8)
            {
              config.SlotConfig[slot] = ATCA_SLOT_CONFIG_EXT_SIG_MASK
                                        | ATCA_SLOT_CONFIG_INT_SIG_MASK
                                        | ATCA_SLOT_CONFIG_IS_SECRET_MASK
                                        | ATCA_SLOT_CONFIG_WRITE_KEY(10)
                                        | ATCA_SLOT_CONFIG_WRITE_CONFIG(6);
              config.KeyConfig[slot] = ATCA_KEY_CONFIG_PRIVATE_MASK
                                       | ATCA_KEY_CONFIG_PUB_INFO_MASK
                                       | ATCA_KEY_CONFIG_KEY_TYPE(4)
                                       | ATCA_KEY_CONFIG_LOCKABLE_MASK
                                       | ATCA_KEY_CONFIG_REQ_RANDOM_MASK;
            }
          else
            {
              config.SlotConfig[slot] = ATCA_SLOT_CONFIG_READKEY(15)
                                        | ATCA_SLOT_CONFIG_WRITE_KEY(15);
              config.KeyConfig[slot] = ATCA_KEY_CONFIG_KEY_TYPE(7)
                                       | ATCA_KEY_CONFIG_LOCKABLE_MASK;
              if (slot == 10)
                {
                  config.SlotConfig[slot] |= ATCA_SLOT_CONFIG_IS_SECRET_MASK;
                  config.KeyConfig[slot] |= ATCA_KEY_CONFIG_REQ_RANDOM_MASK;
                }
            }
        }
      result = atcab_write_bytes_zone(ATCA_ZONE_CONFIG,
                                      0,
                                      offsetof(atecc608_config_t, SlotConfig),
                                      (const uint8_t *)config.SlotConfig,
                                      sizeof(config.SlotConfig));
    }
  if (result == ATCA_SUCCESS)
    {
      result = atcab_write_bytes_zone(ATCA_ZONE_CONFIG,
                                      0,
                                      offsetof(atecc608_config_t, KeyConfig),
                                      (const uint8_t *)config.KeyConfig,
                                      sizeof(config.KeyConfig));
    }
  if (result == ATCA_SUCCESS)
    {
      uint8_t crc[2];
      atCRC(sizeof(config), (const uint8_t *)&config, crc);
      result = atcab_lock_config_zone_crc((uint16_t)crc[0]
                                          | ((uint16_t)crc[1] << 8));
    }
  if (result == ATCA_SUCCESS)
    {
      result = atcab_lock_data_zone();
    }
  for (uint16_t slot = 0; result == ATCA_SUCCESS && slot < 8; ++slot)
    {
      result = atcab_genkey(slot, NULL);
    }
  if (result == ATCA_SUCCESS)
    {
      result = atcab_random(protection_key);
    }
  if (result == ATCA_SUCCESS)
    {
      result = atcab_write_bytes_zone(
          ATCA_ZONE_DATA, 10, 0, protection_key, sizeof(protection_key));
    }
  atcab_memset_s(
      protection_key, sizeof(protection_key), 0, sizeof(protection_key));
  int sleep_result = atcab_wakeup();
  if (sleep_result == ATCA_SUCCESS)
    {
      sleep_result = atcab_sleep();
    }
  crypto_unlock();
  if (result == ATCA_SUCCESS)
    {
      result = sleep_result;
    }
report:
  if (result != ATCA_SUCCESS)
    {
      return stream_error(stream_printf(stream,
                                        "ATECC608B initialization failed: %d "
                                        "(0x%02x), check crypto status\r\n",
                                        result,
                                        (unsigned int)(uint8_t)result))
                 ? CMD_EXIT_IOERR
                 : 1;
    }
  return stream_error(stream_puts(stream, "ATECC608B initialized\r\n"))
             ? CMD_EXIT_IOERR
             : CMD_EXIT_OK;
}

static int
cmd_usb_power_status_encode(cmd_node_t *cmd, stream_t *stream)
{
  usb_t usb = container_of(cmd, cmd_usb_node_t, node)->usb;
  uint32_t contract_voltage_mv, contract_current_ma;
  tcpm_get_power_contract(
      tcpm, usb, &contract_voltage_mv, &contract_current_ma);
  uint32_t voltage_mv = adc_get_usb_voltage(usb);
  uint32_t current_ma = adc_get_usb_current_in(usb);
  uint32_t power_mw
      = (uint32_t)(((uint64_t)voltage_mv * current_ma + 500U) / 1000U);
  stream_status_t result
      = contract_voltage_mv == 0U
            ? stream_puts(stream, "Established contract: none\r\n")
            : stream_printf(stream,
                            "Established contract: %lu.%03lu V, "
                            "%lu.%03lu A\r\n",
                            (unsigned long)(contract_voltage_mv / 1000U),
                            (unsigned long)(contract_voltage_mv % 1000U),
                            (unsigned long)(contract_current_ma / 1000U),
                            (unsigned long)(contract_current_ma % 1000U));
  if (stream_error(result))
    {
      return CMD_EXIT_IOERR;
    }
  return stream_error(stream_printf(stream,
                                    "Voltage: %lu.%03lu V\r\n"
                                    "Current in: %lu.%03lu A\r\n"
                                    "Power in: %lu.%03lu W\r\n",
                                    (unsigned long)(voltage_mv / 1000U),
                                    (unsigned long)(voltage_mv % 1000U),
                                    (unsigned long)(current_ma / 1000U),
                                    (unsigned long)(current_ma % 1000U),
                                    (unsigned long)(power_mw / 1000U),
                                    (unsigned long)(power_mw % 1000U)))
             ? CMD_EXIT_IOERR
             : CMD_EXIT_OK;
}

static int
cmd_magi_controller_hostname_encode(cmd_node_t *cmd, stream_t *stream)
{
  char hostname[NETWORK_HOSTNAME_MAX + 1];
  network_get_hostname(hostname);
  return stream_error(stream_printf(stream, "%s\r\n", hostname))
             ? CMD_EXIT_IOERR
             : CMD_EXIT_OK;
}

static int
cmd_magi_controller_hostname_decode(cmd_node_t *cmd, stream_t *stream)
{
  char hostname[NETWORK_HOSTNAME_MAX + 1];
  uint32_t length = 0;
  stream_status_t result = 0;

  result = stream_gets(
      stream, (uint8_t *)hostname, NETWORK_HOSTNAME_MAX + 1, &length);

  if (result & STREAM_ABORT)
    {
      return CMD_EXIT_INTERRUPT;
    }
  if (stream_error(result & ~STREAM_END))
    {
      return CMD_EXIT_IOERR;
    }

  if (length == 0 || length > NETWORK_HOSTNAME_MAX)
    {
      return stream_error(stream_printf(
                 stream, "hostname must between 1 to 32 characters\r\n"))
                 ? CMD_EXIT_IOERR
                 : 1;
    }
  hostname[length] = '\0';
  int error = memchr(hostname, '\0', length) != NULL
                  ? EINVAL
                  : network_set_hostname(hostname);
  if (error != 0)
    {
      if (error == EINVAL)
        {
          return stream_error(stream_puts(stream, "hostname invalid\r\n"))
                     ? CMD_EXIT_IOERR
                     : 1;
        }
      return stream_error(stream_puts(stream, "failed to update hostname\r\n"))
                 ? CMD_EXIT_IOERR
                 : 1;
    }
  char default_hostname[NETWORK_HOSTNAME_SIZE];
  network_default_hostname(default_hostname);
  cmd->dirty = strcmp(hostname, default_hostname) != 0;
  return CMD_EXIT_OK;
}

static int
cmd_magi_controller_hostname_save(cmd_node_t *cmd, stream_t *stream)
{
  char hostname[NETWORK_HOSTNAME_MAX + 1];
  network_get_hostname(hostname);
  size_t length = strlen(hostname);
  stream_status_t status = config_write_vint(stream, length);
  if (stream_error(status) || !(status & STREAM_WRITE_OK))
    {
      return CMD_EXIT_IOERR;
    }
  return stream_error(stream_puts(stream, hostname)) ? CMD_EXIT_IOERR
                                                     : CMD_EXIT_OK;
}

static int
cmd_magi_controller_hostname_load(cmd_node_t *cmd, stream_t *stream)
{
  uint32_t length;
  stream_status_t status = config_read_vint(stream, &length);
  if (stream_error(status) || !(status & STREAM_READ_OK) || length == 0
      || length > NETWORK_HOSTNAME_MAX)
    {
      return CMD_EXIT_IOERR;
    }
  char hostname[NETWORK_HOSTNAME_MAX + 1];
  uint32_t count = 0;
  status = stream_gets(stream, (uint8_t *)hostname, length, &count);
  if (stream_error(status) || count != length
      || memchr(hostname, '\0', length) != NULL)
    {
      return CMD_EXIT_IOERR;
    }
  hostname[length] = '\0';
  if (network_set_hostname(hostname) != 0)
    {
      return CMD_EXIT_IOERR;
    }
  char default_hostname[NETWORK_HOSTNAME_SIZE];
  network_default_hostname(default_hostname);
  cmd->dirty = strcmp(hostname, default_hostname) != 0;
  return CMD_EXIT_OK;
}

static int
cmd_network_address_encode(stream_t *stream, bool (*get_address)(char *))
{
  char value[NETWORK_ADDRESS_SIZE];
  if (!get_address(value))
    {
      return stream_error(stream_printf(stream, "none\r\n")) ? CMD_EXIT_IOERR
                                                             : 1;
    }
  return stream_error(stream_printf(stream, "%s\r\n", value)) ? CMD_EXIT_IOERR
                                                              : CMD_EXIT_OK;
}

static int
cmd_network_ipv4_address_encode(cmd_node_t *cmd, stream_t *stream)
{
  return cmd_network_address_encode(stream, network_get_ipv4_address);
}

static int
cmd_network_ipv6_link_local_encode(cmd_node_t *cmd, stream_t *stream)
{
  return cmd_network_address_encode(stream,
                                    network_get_ipv6_link_local_address);
}

static int
cmd_network_ipv6_address_encode(cmd_node_t *cmd, stream_t *stream)
{
  return cmd_network_address_encode(stream, network_get_ipv6_address);
}

static int
cmd_magi_controller_version_encode(cmd_node_t *cmd, stream_t *stream)
{
  return stream_error(
             stream_printf(stream,
                           "Board version: 0x%04x\r\n"
                           "Bootloader version: %u.%u.%u\r\n"
                           "Firmware version: %u.%u.%u\r\n",
                           (unsigned int)HEADER_FIRMWARE->board_version,
                           (unsigned int)HEADER_BOOTLOADER->major_version,
                           (unsigned int)HEADER_BOOTLOADER->minor_version,
                           (unsigned int)HEADER_BOOTLOADER->patch_version,
                           (unsigned int)HEADER_FIRMWARE->major_version,
                           (unsigned int)HEADER_FIRMWARE->minor_version,
                           (unsigned int)HEADER_FIRMWARE->patch_version))
             ? CMD_EXIT_IOERR
             : CMD_EXIT_OK;
}

static int
cmd_magi_controller_temperature_encode(cmd_node_t *cmd, stream_t *stream)
{
  int32_t temp = adc_get_temperature();
  return stream_error(stream_printf(stream, "%ld deg C\r\n", (long)temp)) ? 1
                                                                          : 0;
};

static int
cmd_magi_controller_echo_exec(cmd_node_t *cmd, stream_t *stream)
{
  bool previous_cr = false;
  while (true)
    {
      uint8_t c = 0;
      stream_status_t result = stream_getc(stream, &c);
      if (result & STREAM_ABORT)
        {
          return CMD_EXIT_INTERRUPT;
        }
      if (stream_error(result))
        {
          return CMD_EXIT_IOERR;
        }
      if (!(result & STREAM_READ_OK))
        {
          continue;
        }
      bool skip_lf = previous_cr && c == '\n';
      previous_cr = c == '\r';
      if (skip_lf)
        {
          continue;
        }
      if (c == 0x03)
        {
          return stream_error(stream_printf(stream, "^C\r\n"))
                     ? CMD_EXIT_IOERR
                     : CMD_EXIT_INTERRUPT;
        }
      if (c == '\r' || c == '\n')
        {
          if (stream_error(stream_printf(stream, "\r\n")))
            {
              return CMD_EXIT_IOERR;
            }
          continue;
        }
      do
        {
          result = stream_putc(stream, c);
          if (result & STREAM_ABORT)
            {
              return CMD_EXIT_INTERRUPT;
            }
          if (stream_error(result))
            {
              return CMD_EXIT_IOERR;
            }
        }
      while (!(result & STREAM_WRITE_OK));
    }
}

static cmd_node_t cmd_network_ipv6_address = {
  .symbol = "address",
  .encode = cmd_network_ipv6_address_encode,
};
static cmd_node_t cmd_network_ipv6_link_local = {
  .symbol = "link-local-address",
  .encode = cmd_network_ipv6_link_local_encode,
  .next = &cmd_network_ipv6_address,
};
static cmd_node_t cmd_network_ipv6 = {
  .symbol = "ipv6",
  .child = &cmd_network_ipv6_link_local,
};
static cmd_node_t cmd_network_ipv4_address = {
  .symbol = "address",
  .encode = cmd_network_ipv4_address_encode,
};
static cmd_node_t cmd_network_ipv4 = {
  .symbol = "ipv4",
  .child = &cmd_network_ipv4_address,
  .next = &cmd_network_ipv6,
};
static int
cmd_ssh_key_encode(stream_t *stream, bool host)
{
  char key[SSH_KEY_BASE64_SIZE];
  int error = host ? ssh_get_host_key(key) : ssh_get_authorized_key(key);
  if (error != 0)
    {
      stream_puts(stream,
                  error == ENOENT ? "No authorized key configured\r\n"
                                  : "SSH key unavailable: check crypto status "
                                    "and slot provisioning\r\n");
      return CMD_EXIT_IOERR;
    }
  return stream_error(stream_printf(stream, "ecdsa-sha2-nistp256 %s\r\n", key))
             ? CMD_EXIT_IOERR
             : CMD_EXIT_OK;
}

static int
cmd_ssh_host_encode(cmd_node_t *cmd, stream_t *stream)
{
  return cmd_ssh_key_encode(stream, true);
}

static int
cmd_ssh_authorized_encode(cmd_node_t *cmd, stream_t *stream)
{
  return cmd_ssh_key_encode(stream, false);
}

static int
cmd_ssh_authorized_decode(cmd_node_t *cmd, stream_t *stream)
{
  char input[CMD_LINE_MAX + 1];
  uint32_t length = 0;
  stream_status_t result
      = stream_gets(stream, (uint8_t *)input, sizeof(input) - 1, &length);
  if (result & STREAM_ABORT)
    {
      return CMD_EXIT_INTERRUPT;
    }
  if (stream_error(result & ~STREAM_END))
    {
      return CMD_EXIT_IOERR;
    }
  input[length] = '\0';
  char *key = input;
  size_t key_length = strcspn(input, " \t");
  int error = EINVAL;
  if ((result & STREAM_END) && memchr(input, '\0', length) == NULL)
    {
      if (length == 0)
        {
          error = ssh_clear_authorized_key();
        }
      else if (key_length < length)
        {
          input[key_length] = '\0';
          if (strcmp(input, "ecdsa-sha2-nistp256") == 0)
            {
              key += key_length + 1;
              key += strspn(key, " \t");
              key_length = strcspn(key, " \t");
              error = ssh_set_authorized_key(key, key_length);
            }
        }
    }
  if (error != 0)
    {
      stream_puts(
          stream,
          error == EINVAL
              ? "Expected a full ECDSA P-256 public key or an empty string\r\n"
              : "Authorized key write failed: check slot 9 provisioning\r\n");
      return error == EINVAL ? CMD_EXIT_MISUSE : CMD_EXIT_IOERR;
    }
  return CMD_EXIT_OK;
}

static cmd_node_t cmd_ssh_host = {
  .symbol = "host-key",
  .encode = cmd_ssh_host_encode,
};
static cmd_node_t cmd_ssh_authorized = {
  .symbol = "authorized-key",
  .encode = cmd_ssh_authorized_encode,
  .decode = cmd_ssh_authorized_decode,
  .next = &cmd_ssh_host,
};
static cmd_node_t cmd_magi_controller_ssh = {
  .symbol = "ssh",
  .child = &cmd_ssh_authorized,
};
static cmd_node_t cmd_magi_crypto_init = {
  .symbol = "init",
  .exec = cmd_magi_crypto_init_exec,
};
static cmd_node_t cmd_magi_crypto_status = {
  .symbol = "status",
  .encode = cmd_magi_crypto_status_encode,
  .next = &cmd_magi_crypto_init,
};
static cmd_node_t cmd_magi_controller_network = {
  .symbol = "network",
  .child = &cmd_network_ipv4,
  .next = &cmd_magi_controller_ssh,
};

static cmd_node_t cmd_magi_controller_echo = {
  .symbol = "echo",
  .exec = cmd_magi_controller_echo_exec,
  .next = &cmd_magi_controller_network,
};

static cmd_node_t cmd_magi_controller_hostname = {
  .symbol = "hostname",
  .encode = cmd_magi_controller_hostname_encode,
  .decode = cmd_magi_controller_hostname_decode,
  .save = cmd_magi_controller_hostname_save,
  .load = cmd_magi_controller_hostname_load,
  .next = &cmd_magi_controller_echo,
};

static cmd_node_t cmd_magi_controller_version = {
  .symbol = "version",
  .encode = cmd_magi_controller_version_encode,
  .next = &cmd_magi_controller_hostname,
};

static cmd_node_t cmd_magi_controller_temperature = {
  .symbol = "temperature",
  .encode = &cmd_magi_controller_temperature_encode,
  .next = &cmd_magi_controller_version,
};

static cmd_node_t cmd_unit3_console
    = { .symbol = "console", .exec = cmd_unit3_console_exec };
static cmd_node_t cmd_unit2_console
    = { .symbol = "console", .exec = cmd_unit2_console_exec };
static cmd_node_t cmd_unit1_console
    = { .symbol = "console", .exec = cmd_unit1_console_exec };

static cmd_unit_node_t cmd_unit1_power_button_press = {
  .node = { .symbol = "power-button-press",
            .encode = cmd_unit_power_button_press_encode,
            .decode = cmd_unit_power_button_press_decode,
            .next = &cmd_unit1_console },
  .unit = &unit_1,
};
static cmd_unit_node_t cmd_unit1_rpiboot = {
  .node = { .symbol = "rpiboot",
            .encode = cmd_unit_rpiboot_encode,
            .decode = cmd_unit_rpiboot_decode,
            .next = &cmd_unit1_power_button_press.node },
  .unit = &unit_1,
};
static cmd_unit_node_t cmd_unit1_power = {
  .node = { .symbol = "power",
            .encode = cmd_unit_power_encode,
            .decode = cmd_unit_power_decode,
            .next = &cmd_unit1_rpiboot.node },
  .unit = &unit_1,
};
static cmd_unit_node_t cmd_unit2_power_button_press = {
  .node = { .symbol = "power-button-press",
            .encode = cmd_unit_power_button_press_encode,
            .decode = cmd_unit_power_button_press_decode,
            .next = &cmd_unit2_console },
  .unit = &unit_2,
};
static cmd_unit_node_t cmd_unit2_rpiboot = {
  .node = { .symbol = "rpiboot",
            .encode = cmd_unit_rpiboot_encode,
            .decode = cmd_unit_rpiboot_decode,
            .next = &cmd_unit2_power_button_press.node },
  .unit = &unit_2,
};
static cmd_unit_node_t cmd_unit2_power = {
  .node = { .symbol = "power",
            .encode = cmd_unit_power_encode,
            .decode = cmd_unit_power_decode,
            .next = &cmd_unit2_rpiboot.node },
  .unit = &unit_2,
};
static cmd_unit_node_t cmd_unit3_power_button_press = {
  .node = { .symbol = "power-button-press",
            .encode = cmd_unit_power_button_press_encode,
            .decode = cmd_unit_power_button_press_decode,
            .next = &cmd_unit3_console },
  .unit = &unit_3,
};
static cmd_unit_node_t cmd_unit3_rpiboot = {
  .node = { .symbol = "rpiboot",
            .encode = cmd_unit_rpiboot_encode,
            .decode = cmd_unit_rpiboot_decode,
            .next = &cmd_unit3_power_button_press.node },
  .unit = &unit_3,
};
static cmd_unit_node_t cmd_unit3_power = {
  .node = { .symbol = "power",
            .encode = cmd_unit_power_encode,
            .decode = cmd_unit_power_decode,
            .next = &cmd_unit3_rpiboot.node },
  .unit = &unit_3,
};

static cmd_unit_node_t cmd_unit1_type = {
  .node = { .symbol = "type",
            .encode = cmd_unit_type_encode,
            .decode = cmd_unit_type_decode,
            .save = cmd_unit_type_save,
            .load = cmd_unit_type_load,
            .next = &cmd_unit1_power.node },
  .unit = &unit_1,
};
static cmd_unit_node_t cmd_unit2_type = {
  .node = { .symbol = "type",
            .encode = cmd_unit_type_encode,
            .decode = cmd_unit_type_decode,
            .save = cmd_unit_type_save,
            .load = cmd_unit_type_load,
            .next = &cmd_unit2_power.node },
  .unit = &unit_2,
};
static cmd_unit_node_t cmd_unit3_type = {
  .node = { .symbol = "type",
            .encode = cmd_unit_type_encode,
            .decode = cmd_unit_type_decode,
            .save = cmd_unit_type_save,
            .load = cmd_unit_type_load,
            .next = &cmd_unit3_power.node },
  .unit = &unit_3,
};

static cmd_node_t cmd_unit3 = { .symbol = "3", .child = &cmd_unit3_type.node };
static cmd_node_t cmd_unit2
    = { .symbol = "2", .child = &cmd_unit2_type.node, .next = &cmd_unit3 };
static cmd_node_t cmd_unit1
    = { .symbol = "1", .child = &cmd_unit1_type.node, .next = &cmd_unit2 };
static cmd_usb_node_t cmd_usb0_power_status = {
  .node = { .symbol = "status", .encode = cmd_usb_power_status_encode },
  .usb = USB0,
};
static cmd_usb_node_t cmd_usb1_power_status = {
  .node = { .symbol = "status", .encode = cmd_usb_power_status_encode },
  .usb = USB1,
};
static cmd_node_t cmd_usb0_power = {
  .symbol = "power",
  .child = &cmd_usb0_power_status.node,
};
static cmd_node_t cmd_usb1_power = {
  .symbol = "power",
  .child = &cmd_usb1_power_status.node,
};
static cmd_node_t cmd_usb1 = {
  .symbol = "1",
  .child = &cmd_usb1_power,
};
static cmd_node_t cmd_usb0 = {
  .symbol = "0",
  .child = &cmd_usb0_power,
  .next = &cmd_usb1,
};
static int
cmd_magi_config_save_exec(cmd_node_t *cmd, stream_t *stream)
{
  if (config_save() != 0)
    {
      stream_puts(stream, "Failed to save configuration\r\n");
      return CMD_EXIT_IOERR;
    }
  return stream_error(stream_puts(stream, "Configuration saved\r\n"))
             ? CMD_EXIT_IOERR
             : CMD_EXIT_OK;
}

static int
cmd_magi_config_reset_exec(cmd_node_t *cmd, stream_t *stream)
{
  if (config_reset() != 0)
    {
      stream_puts(stream, "Failed to reset configuration\r\n");
      return CMD_EXIT_IOERR;
    }
  return stream_error(stream_puts(stream, "Configuration reset\r\n"))
             ? CMD_EXIT_IOERR
             : CMD_EXIT_OK;
}

static cmd_node_t cmd_magi_config_reset = {
  .symbol = "reset",
  .exec = cmd_magi_config_reset_exec,
};
static cmd_node_t cmd_magi_config_save = {
  .symbol = "save",
  .exec = cmd_magi_config_save_exec,
  .next = &cmd_magi_config_reset,
};
static cmd_node_t cmd_magi_config = {
  .symbol = "config",
  .child = &cmd_magi_config_save,
};
static cmd_node_t cmd_magi_usb = {
  .symbol = "usb",
  .child = &cmd_usb0,
  .next = &cmd_magi_config,
};
static cmd_node_t cmd_magi_unit = {
  .symbol = "unit",
  .child = &cmd_unit1,
  .next = &cmd_magi_usb,
};

static cmd_node_t cmd_magi_crypto = {
  .symbol = "crypto",
  .child = &cmd_magi_crypto_status,
  .next = &cmd_magi_unit,
};

static cmd_node_t cmd_magi_controller = {
  .symbol = "controller",
  .child = &cmd_magi_controller_temperature,
  .next = &cmd_magi_crypto,
};

static cmd_node_t cmd_root_instance = {
  .symbol = "magi",
  .child = &cmd_magi_controller,
};

cmd_node_t *cmd_root = &cmd_root_instance;

typedef enum
{
  CMD_LINE_OK = 0,
  CMD_LINE_ABORT,
  CMD_LINE_OVERFLOW,
  CMD_LINE_ERROR,
} cmd_line_status_t;

typedef enum
{
  CMD_STATE_WALK = 0,
  CMD_STATE_GET_KEY,
  CMD_STATE_GET,
  CMD_STATE_SET_KEY,
  CMD_STATE_SET,
  CMD_STATE_EXEC,
  CMD_STATE_ERROR,
  CMD_STATE_DONE,
} cmd_state_t;

typedef enum
{
  CMD_ERROR_OK = 0,
  CMD_ERROR_ABORT,
  CMD_ERROR_UNSUPPORTED_OPERATION,
  CMD_ERROR_INCORRECT_NUMBER_ARGUMENTS,
  CMD_ERROR_UNKNOWN_COMMAND,
  CMD_ERROR_SYMBOL_TOO_LONG,
  CMD_ERROR_UNTERMINATED_QUOTE,
} cmd_error_t;

typedef enum
{
  CMD_SYMBOL_OK = 0,
  CMD_SYMBOL_OVERFLOW,
  CMD_SYMBOL_ABORT,
  CMD_SYMBOL_ERROR,
} cmd_symbol_result_t;

typedef struct
{
  uint8_t line[CMD_LINE_MAX];
  uint32_t write_count;
  uint8_t new_line;
  stream_t *io;
  uint32_t read_count;
  stream_t stream;
} cmd_line_t;

typedef enum
{
  CMD_ARGUMENT_PREFIX = 0,
  CMD_ARGUMENT_VALUE,
  CMD_ARGUMENT_QUOTED,
  CMD_ARGUMENT_SUFFIX,
} cmd_argument_state_t;

typedef struct
{
  stream_t *io;
  cmd_line_t *line;
  cmd_node_t *cmd;
  cmd_state_t state;
  uint8_t symbol[CMD_SYMBOL_MAX + 2];
  uint32_t symbol_len;
  bool end;
  cmd_error_t error;
  int exit_code;
} cmd_exec_t;

typedef struct
{
  cmd_exec_t *exec;
  stream_t stream;
} cmd_exec_stream_t;

typedef struct
{
  stream_t *io;
  cmd_exec_t *exec;
  cmd_argument_state_t state;
  stream_status_t status;
  stream_t stream;
} cmd_argument_t;

static stream_status_t
cmd_line_stream_exchange(stream_t *stream,
                         uint8_t *read,
                         uint8_t *write,
                         stream_opts_t *opts)
{
  cmd_line_t *line = container_of(stream, cmd_line_t, stream);
  stream_status_t result = 0;

  if (read)
    {
      if (line->read_count < line->write_count)
        {
          *read = line->line[line->read_count];
          line->read_count += 1;
          result |= STREAM_READ_OK;
        }
      else
        {
          result |= STREAM_END;
        }
    }
  if (write)
    {
      stream_opts_t nonblocking;
      if (opts != NULL)
        {
          nonblocking = *opts;
        }
      else
        {
          stream_opts_init(&nonblocking);
        }
      nonblocking.nonblocking = true;
      result |= line->io->exchange(
          line->io, NULL, write, result != 0 ? &nonblocking : opts);
    }
  return result;
}

static stream_status_t
cmd_exec_stream_exchange(stream_t *stream,
                         uint8_t *read,
                         uint8_t *write,
                         stream_opts_t *opts)
{
  cmd_exec_stream_t *exec_stream
      = container_of(stream, cmd_exec_stream_t, stream);
  cmd_exec_t *exec = exec_stream->exec;
  if (!read)
    {
      return exec->io->exchange(exec->io, NULL, write, opts);
    }
  while (true)
    {
      uint8_t c = 0;
      stream_status_t result = exec->io->exchange(exec->io, &c, write, opts);
      if (!(result & STREAM_READ_OK))
        {
          return result;
        }
      bool skip_lf = exec->line->new_line == '\r' && c == '\n';
      exec->line->new_line = 0;
      if (!skip_lf)
        {
          *read = c;
          return result;
        }
      result &= ~STREAM_READ_OK;
      if (result != 0)
        {
          return result;
        }
    }
}

static void
cmd_line_stream_notify(stream_t *stream, uint32_t read, uint32_t write)
{
  cmd_line_t *line = container_of(stream, cmd_line_t, stream);
  line->io->notify(line->io, 0, write);
  if (read)
    {
      xTaskNotify(xTaskGetCurrentTaskHandle(), read, eSetBits);
    }
}

static void
cmd_exec_stream_notify(stream_t *stream, uint32_t read, uint32_t write)
{
  cmd_exec_stream_t *exec_stream
      = container_of(stream, cmd_exec_stream_t, stream);
  exec_stream->exec->io->notify(exec_stream->exec->io, read, write);
}

static void
cmd_argument_stream_notify(stream_t *stream, uint32_t read, uint32_t write)
{
  cmd_argument_t *argument = container_of(stream, cmd_argument_t, stream);
  argument->io->notify(argument->io, argument->status ? 0 : read, write);
  if (read && argument->status)
    {
      xTaskNotify(xTaskGetCurrentTaskHandle(), read, eSetBits);
    }
}

static void
cmd_line_init(cmd_line_t *line, stream_t *stream)
{
  line->io = stream;
  line->write_count = 0;
  line->new_line = 0;
  line->read_count = 0;
  line->stream.exchange = cmd_line_stream_exchange;
  line->stream.notify = cmd_line_stream_notify;
}

static void
cmd_line_reset(cmd_line_t *line)
{
  line->read_count = 0;
  line->write_count = 0;
}

static cmd_line_status_t
cmd_line_readline(cmd_line_t *line)
{
  enum
  {
    ESCAPE_NONE,
    ESCAPE_START,
    ESCAPE_SEQUENCE,
  } escape
      = ESCAPE_NONE;
  uint8_t c = 0;
  stream_status_t result = 0;
  cmd_line_reset(line);

  while (true)
    {
      result = stream_getc(line->io, &c);
      if (stream_error(result))
        {
          return CMD_LINE_ERROR;
        }
      if (!(result & STREAM_READ_OK))
        {
          continue;
        }
      bool skip_lf = line->new_line == '\r' && c == '\n';
      line->new_line = 0;
      if (skip_lf)
        {
          continue;
        }
      break;
    }
  while (true)
    {
      switch (c)
        {
        case '\r':
        case '\n':
          line->new_line = c;
          stream_puts(line->io, "\r\n");
          return CMD_LINE_OK;
        case 0x03:
          stream_puts(line->io, "^C\r\n");
          return CMD_LINE_ABORT;
        case '\b':
        case 0x7f:
          escape = ESCAPE_NONE;
          if (line->write_count > 0)
            {
              line->write_count--;
              if (stream_error(stream_printf(line->io, "\b \b")))
                {
                  return CMD_LINE_ERROR;
                }
            }
          do
            {
              result = stream_getc(line->io, &c);
              if (stream_error(result))
                {
                  return CMD_LINE_ERROR;
                }
            }
          while (!(result & STREAM_READ_OK));
          continue;
        default:
          break;
        }
      bool accept = c >= 0x20 && c <= 0x7e;
      if (c == 0x1b)
        {
          escape = ESCAPE_START;
          accept = false;
        }
      else if (escape == ESCAPE_START)
        {
          escape = c == '[' || c == 'O' ? ESCAPE_SEQUENCE : ESCAPE_NONE;
          accept = false;
        }
      else if (escape == ESCAPE_SEQUENCE)
        {
          if (c >= 0x40 && c <= 0x7e)
            {
              escape = ESCAPE_NONE;
            }
          accept = false;
        }
      if (accept && line->write_count >= CMD_LINE_MAX)
        {
          while (true)
            {
              switch (c)
                {
                case '\r':
                case '\n':
                  line->new_line = c;
                  stream_puts(line->io, "\r\n");
                  return CMD_LINE_OVERFLOW;
                case 0x03:
                  stream_puts(line->io, "^C\r\n");
                  return CMD_LINE_ABORT;
                default:
                  result = stream_getc(line->io, &c);
                  if (stream_error(result))
                    {
                      return CMD_LINE_ERROR;
                    }
                }
            }
        }
      if (accept)
        {
          line->line[line->write_count] = c;
          line->write_count++;
        }
      uint8_t next = 0;
      bool read = true;
      bool write = accept;
      do
        {
          result = line->io->exchange(
              line->io, read ? &next : NULL, write ? &c : NULL, NULL);
          if (result & STREAM_READ_OK)
            {
              read = false;
            }
          if (result & STREAM_WRITE_OK)
            {
              write = false;
            }
          if (stream_error(result))
            {
              return CMD_LINE_ERROR;
            }
        }
      while (read || write);
      c = next;
    }
}

static bool
cmd_is_whitespace(uint8_t c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static stream_status_t
cmd_argument_stream_exchange(stream_t *stream,
                             uint8_t *read,
                             uint8_t *write,
                             stream_opts_t *opts)
{
  cmd_argument_t *argument = container_of(stream, cmd_argument_t, stream);
  if (!read || argument->status != 0)
    {
      stream_opts_t nonblocking;
      if (opts != NULL)
        {
          nonblocking = *opts;
        }
      else
        {
          stream_opts_init(&nonblocking);
        }
      nonblocking.nonblocking = true;
      stream_status_t result
          = write ? argument->io->exchange(
                        argument->io, NULL, write, read ? &nonblocking : opts)
                  : 0;
      return result | (read ? argument->status : 0);
    }

  while (true)
    {
      uint8_t c = 0;
      stream_status_t result
          = argument->io->exchange(argument->io, &c, write, opts);
      if (((result & STREAM_END) && argument->state == CMD_ARGUMENT_PREFIX)
          || (argument->state == CMD_ARGUMENT_QUOTED
              && ((result & STREAM_END)
                  || ((result & STREAM_READ_OK) && (c == '\r' || c == '\n')))))
        {
          argument->exec->end = (result & STREAM_END) != 0;
          argument->exec->state = CMD_STATE_ERROR;
          argument->exec->error = argument->state == CMD_ARGUMENT_QUOTED
                                      ? CMD_ERROR_UNTERMINATED_QUOTE
                                      : CMD_ERROR_INCORRECT_NUMBER_ARGUMENTS;
          argument->status = STREAM_ABORT;
          return (result & STREAM_WRITE_OK) | STREAM_ABORT;
        }
      if (stream_error(result))
        {
          argument->status = result & ~(STREAM_READ_OK | STREAM_WRITE_OK);
          if (result & STREAM_END)
            {
              argument->exec->end = true;
            }
          return result & ~STREAM_READ_OK;
        }
      if (!(result & STREAM_READ_OK))
        {
          return result;
        }
      if (argument->state == CMD_ARGUMENT_QUOTED)
        {
          if (c != '"')
            {
              *read = c;
              return result;
            }
          argument->state = CMD_ARGUMENT_SUFFIX;
        }
      else if (argument->state == CMD_ARGUMENT_PREFIX && c == '"')
        {
          argument->state = CMD_ARGUMENT_QUOTED;
        }
      else if (cmd_is_whitespace(c))
        {
          if (argument->state == CMD_ARGUMENT_VALUE)
            {
              argument->state = CMD_ARGUMENT_SUFFIX;
            }
        }
      else if (argument->state == CMD_ARGUMENT_SUFFIX)
        {
          argument->exec->state = CMD_STATE_ERROR;
          argument->exec->error = CMD_ERROR_INCORRECT_NUMBER_ARGUMENTS;
          argument->status = STREAM_ABORT;
          return (result & STREAM_WRITE_OK) | STREAM_ABORT;
        }
      else
        {
          argument->state = CMD_ARGUMENT_VALUE;
          *read = c;
          return result;
        }
      if (result & STREAM_WRITE_OK)
        {
          return STREAM_WRITE_OK;
        }
    }
}

static void
cmd_argument_init(cmd_argument_t *argument, stream_t *stream, cmd_exec_t *exec)
{
  argument->io = stream;
  argument->exec = exec;
  argument->state = CMD_ARGUMENT_PREFIX;
  argument->status = 0;
  argument->stream.exchange = cmd_argument_stream_exchange;
  argument->stream.notify = cmd_argument_stream_notify;
}

static bool
cmd_symbol_equal(const char *symbol, uint8_t *input, uint32_t n)
{
  return strlen(symbol) == n && memcmp(symbol, input, n) == 0;
}

static cmd_symbol_result_t
cmd_read_symbol(cmd_exec_t *exec)
{
  stream_t *stream = &exec->line->stream;
  uint8_t c = 0;
  stream_status_t result = 0;
  exec->symbol_len = 0;
  exec->symbol[0] = '\0';

  while (true)
    {
      result = stream_getc(stream, &c);
      if (result & STREAM_END)
        {
          exec->end = true;
          return CMD_SYMBOL_OK;
        }
      if (result & STREAM_ABORT)
        {
          return CMD_SYMBOL_ABORT;
        }

      if (stream_error(result))
        {
          return CMD_SYMBOL_ERROR;
        }
      if (!(result & STREAM_READ_OK))
        {
          continue;
        }
      if (cmd_is_whitespace(c))
        {
          if (exec->symbol_len > 0)
            {
              return CMD_SYMBOL_OK;
            }
          continue;
        }
      if (exec->symbol_len >= CMD_SYMBOL_MAX)
        {
          return CMD_SYMBOL_OVERFLOW;
        }
      exec->symbol[exec->symbol_len] = c;
      exec->symbol_len += 1;
      exec->symbol[exec->symbol_len] = '\0';
    }
}

cmd_node_t *
cmd_search_node(cmd_node_t *cmd, uint8_t *input, uint32_t input_len)
{
  if (cmd == NULL)
    {
      if (cmd_symbol_equal(cmd_root->symbol, input, input_len))
        {
          return cmd_root;
        }
      return NULL;
    }
  cmd_node_t *search = cmd->child;
  while (search != NULL)
    {
      if (cmd_symbol_equal(search->symbol, input, input_len))
        {
          return search;
        }
      search = search->next;
    }
  return NULL;
}

static void
cmd_sm_next_symbol(cmd_exec_t *exec)
{
  cmd_symbol_result_t result = 0;
  result = cmd_read_symbol(exec);
  switch (result)
    {
    case CMD_SYMBOL_OK:
      return;
    case CMD_SYMBOL_ABORT:
      exec->state = CMD_STATE_DONE;
      exec->exit_code = CMD_EXIT_INTERRUPT;
      return;
    case CMD_SYMBOL_OVERFLOW:
      exec->state = CMD_STATE_ERROR;
      exec->error = CMD_ERROR_SYMBOL_TOO_LONG;
      return;
    case CMD_SYMBOL_ERROR:
      exec->state = CMD_STATE_DONE;
      exec->exit_code = CMD_EXIT_IOERR;
    }
}

static void
cmd_sm_error(cmd_exec_t *exec)
{
  stream_t *stream = &exec->line->stream;
  if (!exec->end)
    {
      uint8_t c = 0;
      while (true)
        {
          stream_status_t result = stream_getc(stream, &c);
          if (result & STREAM_ABORT)
            {
              exec->state = CMD_STATE_DONE;
              exec->exit_code = CMD_EXIT_INTERRUPT;
              return;
            }
          if (stream_error(result & ~STREAM_END))
            {
              exec->state = CMD_STATE_DONE;
              exec->exit_code = CMD_EXIT_IOERR;
              return;
            }
          if (result & STREAM_END)
            {
              exec->end = true;
              break;
            }
        }
    }
  switch (exec->error)
    {
    case CMD_ERROR_UNTERMINATED_QUOTE:
      exec->exit_code = CMD_EXIT_MISUSE;
      stream_puts(stream, "unterminated quoted argument\r\n");
      break;
    case CMD_ERROR_SYMBOL_TOO_LONG:
      exec->exit_code = CMD_EXIT_NOT_FOUND;
      stream_puts(stream, "parsing error: symbol too long\r\n");
      break;
    case CMD_ERROR_UNKNOWN_COMMAND:
      exec->exit_code = CMD_EXIT_NOT_FOUND;
      stream_puts(stream, "unknown command '");
      for (uint32_t i = 0; i < exec->symbol_len; i++)
        {
          stream_putc(stream, exec->symbol[i]);
        }
      stream_puts(stream, "'\r\n");
      break;
    case CMD_ERROR_UNSUPPORTED_OPERATION:
      exec->exit_code = CMD_EXIT_NOT_FOUND;
      stream_puts(stream, "unsupported operation for '");
      for (int i = 0; i < exec->symbol_len; i++)
        {
          stream_putc(stream, exec->symbol[i]);
        }
      stream_puts(stream, "' command\r\n");
      break;
    case CMD_ERROR_INCORRECT_NUMBER_ARGUMENTS:
      exec->exit_code = CMD_EXIT_NOT_FOUND;
      stream_puts(stream, "incorrect number of arguments\r\n");
      break;
    case CMD_ERROR_OK:
    case CMD_ERROR_ABORT:
      break;
    }
  exec->state = CMD_STATE_DONE;
}

static void
cmd_sm_walk(cmd_exec_t *exec)
{
  cmd_sm_next_symbol(exec);
  if (exec->state != CMD_STATE_WALK)
    {
      return;
    }

  if (exec->end && exec->symbol_len == 0)
    {
      exec->state = exec->cmd == NULL ? CMD_STATE_DONE : CMD_STATE_EXEC;
      return;
    }

  if (cmd_symbol_equal(CMD_SYMBOL_GET, exec->symbol, exec->symbol_len))
    {
      if (exec->end)
        {
          exec->state = CMD_STATE_ERROR;
          exec->error = CMD_ERROR_INCORRECT_NUMBER_ARGUMENTS;
          return;
        }
      exec->state = CMD_STATE_GET_KEY;
      return;
    }

  if (cmd_symbol_equal(CMD_SYMBOL_SET, exec->symbol, exec->symbol_len))
    {
      if (exec->end)
        {
          exec->state = CMD_STATE_ERROR;
          exec->error = CMD_ERROR_INCORRECT_NUMBER_ARGUMENTS;
          return;
        }
      exec->state = CMD_STATE_SET_KEY;
      return;
    }

  cmd_node_t *next
      = cmd_search_node(exec->cmd, exec->symbol, exec->symbol_len);
  if (next == NULL)
    {
      exec->state = CMD_STATE_ERROR;
      exec->error = CMD_ERROR_UNKNOWN_COMMAND;
      return;
    }

  exec->cmd = next;
  if (exec->end)
    {
      exec->state = CMD_STATE_EXEC;
    }
}

static void
cmd_sm_get_key(cmd_exec_t *exec)
{
  cmd_sm_next_symbol(exec);
  if (exec->state != CMD_STATE_GET_KEY)
    {
      return;
    }

  if (exec->end && exec->symbol_len == 0)
    {
      exec->state = CMD_STATE_ERROR;
      exec->error = CMD_ERROR_INCORRECT_NUMBER_ARGUMENTS;
      return;
    }

  cmd_node_t *get = cmd_search_node(exec->cmd, exec->symbol, exec->symbol_len);
  if (get == NULL)
    {
      exec->state = CMD_STATE_ERROR;
      exec->error = CMD_ERROR_UNKNOWN_COMMAND;
      return;
    }

  exec->cmd = get;
  exec->state = CMD_STATE_GET;
}

static void
cmd_sm_get(cmd_exec_t *exec)
{
  if (!exec->end)
    {
      cmd_sm_next_symbol(exec);
      if (exec->state != CMD_STATE_GET)
        {
          return;
        }
      if (!exec->end || exec->symbol_len != 0)
        {
          exec->state = CMD_STATE_ERROR;
          exec->error = CMD_ERROR_INCORRECT_NUMBER_ARGUMENTS;
          return;
        }
    }
  if (exec->cmd->encode == NULL)
    {
      exec->state = CMD_STATE_ERROR;
      exec->error = CMD_ERROR_UNSUPPORTED_OPERATION;
      return;
    }
  exec->exit_code = exec->cmd->encode(exec->cmd, &exec->line->stream);
  exec->state = CMD_STATE_DONE;
}

static void
cmd_sm_set_key(cmd_exec_t *exec)
{
  cmd_sm_next_symbol(exec);
  if (exec->state != CMD_STATE_SET_KEY)
    {
      return;
    }

  if (exec->end && exec->symbol_len == 0)
    {
      exec->state = CMD_STATE_ERROR;
      exec->error = CMD_ERROR_INCORRECT_NUMBER_ARGUMENTS;
      return;
    }

  cmd_node_t *set = cmd_search_node(exec->cmd, exec->symbol, exec->symbol_len);
  if (set == NULL)
    {
      exec->state = CMD_STATE_ERROR;
      exec->error = CMD_ERROR_UNKNOWN_COMMAND;
      return;
    }
  exec->cmd = set;
  exec->state = CMD_STATE_SET;
}

static void
cmd_sm_set(cmd_exec_t *exec)
{
  if (exec->end)
    {
      exec->state = CMD_STATE_ERROR;
      exec->error = CMD_ERROR_INCORRECT_NUMBER_ARGUMENTS;
      return;
    }
  if (exec->cmd->decode == NULL)
    {
      exec->state = CMD_STATE_ERROR;
      exec->error = CMD_ERROR_UNSUPPORTED_OPERATION;
      return;
    }
  cmd_argument_t argument;
  cmd_argument_init(&argument, &exec->line->stream, exec);
  exec->exit_code = exec->cmd->decode(exec->cmd, &argument.stream);
  if (exec->state == CMD_STATE_SET)
    {
      exec->state = CMD_STATE_DONE;
    }
}

static void
cmd_sm_exec(cmd_exec_t *exec)
{
  if (exec->cmd->exec == NULL)
    {
      exec->state = CMD_STATE_ERROR;
      exec->error = CMD_ERROR_UNSUPPORTED_OPERATION;
      return;
    }
  cmd_exec_stream_t exec_stream = {
    .exec = exec,
    .stream = { .exchange = cmd_exec_stream_exchange,
                .notify = cmd_exec_stream_notify },
  };
  exec->exit_code = exec->cmd->exec(exec->cmd, &exec_stream.stream);
  exec->state = CMD_STATE_DONE;
}

static int
cmd_exec(stream_t *io, cmd_line_t *line)
{
  cmd_exec_t exec = {
    .io = io,
    .line = line,
  };

  while (true)
    {
      switch (exec.state)
        {
        case CMD_STATE_DONE:
          return exec.exit_code;
        case CMD_STATE_WALK:
          cmd_sm_walk(&exec);
          break;
        case CMD_STATE_GET_KEY:
          cmd_sm_get_key(&exec);
          break;
        case CMD_STATE_GET:
          cmd_sm_get(&exec);
          break;
        case CMD_STATE_SET_KEY:
          cmd_sm_set_key(&exec);
          break;
        case CMD_STATE_SET:
          cmd_sm_set(&exec);
          break;
        case CMD_STATE_EXEC:
          cmd_sm_exec(&exec);
          break;
        case CMD_STATE_ERROR:
          cmd_sm_error(&exec);
          break;
        }
    }
}

int
cmd_execute(stream_t *stream, const char *command)
{
  size_t length = strlen(command);
  if (length == 0 || length > CMD_LINE_MAX || strchr(command, '\r') != NULL
      || strchr(command, '\n') != NULL)
    {
      return CMD_EXIT_MISUSE;
    }
  cmd_line_t line;
  cmd_line_init(&line, stream);
  memcpy(line.line, command, length);
  line.write_count = length;
  return cmd_exec(stream, &line);
}

static bool
cmd_line_is_exit(const cmd_line_t *line)
{
  uint32_t start = 0, end = line->write_count;
  while (start < end
         && (line->line[start] == ' ' || line->line[start] == '\t'))
    {
      ++start;
    }
  while (end > start
         && (line->line[end - 1] == ' ' || line->line[end - 1] == '\t'))
    {
      --end;
    }
  return end - start == 4 && memcmp(line->line + start, "exit", 4) == 0;
}

void
cmd_shell(stream_t *stream)
{
  cmd_line_t line;
  cmd_line_init(&line, stream);

  while (true)
    {
      stream_status_t result = 0;
      char hostname[NETWORK_HOSTNAME_MAX + 1];
      network_get_hostname(hostname);
      result = stream_printf(stream, "%s> ", hostname);
      if (stream_error(result))
        {
          return;
        }
      switch (cmd_line_readline(&line))
        {
        case CMD_LINE_OK:
          if (cmd_line_is_exit(&line))
            {
              return;
            }
          cmd_exec(stream, &line);
          break;
        case CMD_LINE_ABORT:
          break;
        case CMD_LINE_OVERFLOW:
          stream_puts(stream, "line buffer overflow\r\n");
          break;
        case CMD_LINE_ERROR:
          return;
        }
    }
}
