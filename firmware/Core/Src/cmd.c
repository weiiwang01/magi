#include <stddef.h>

#include "cmd.h"

#include <string.h>

#include "adc.h"
#include "container_of.h"

#define CMD_LINE_MAX 512
#define CMD_SYMBOL_MAX 32
#define CMD_HOSTNAME_MAX 32

#define CMD_SYMBOL_GET "get"
#define CMD_SYMBOL_SET "set"

static char cmd_hostname[CMD_HOSTNAME_MAX + 1] = "magi";

static int
cmd_magi_controller_hostname_encode(cmd_node_t *cmd, stream_t *stream)
{
  return stream_error(stream_printf(stream, "%s\r\n", cmd_hostname))
             ? CMD_EXIT_IOERR
             : CMD_EXIT_OK;
}

static int
cmd_magi_controller_hostname_decode(cmd_node_t *cmd, stream_t *stream)
{
  char hostname[CMD_HOSTNAME_MAX + 1];
  size_t length = 0;
  bool overflow = false;
  bool invalid_character = false;

  while (true)
    {
      uint8_t c = 0;
      stream_status_t result = stream_getc(stream, &c);
      if (result & STREAM_ABORT)
        {
          return CMD_EXIT_INTERRUPT;
        }
      if (stream_error(result & ~STREAM_END))
        {
          return CMD_EXIT_IOERR;
        }
      if (result & STREAM_END)
        {
          break;
        }
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')
            || (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.'))
        {
          invalid_character = true;
        }
      if (length == CMD_HOSTNAME_MAX)
        {
          overflow = true;
        }
      else
        {
          hostname[length++] = c;
        }
    }

  if (length == 0 || overflow)
    {
      return stream_error(stream_printf(
                 stream, "hostname must between 1 to 32 characters\r\n"))
                 ? CMD_EXIT_IOERR
                 : 1;
    }
  if (invalid_character)
    {
      return stream_error(
                 stream_printf(stream, "invalid characters in hostname\r\n"))
                 ? CMD_EXIT_IOERR
                 : 1;
    }
  hostname[length] = '\0';
  memcpy(cmd_hostname, hostname, length + 1);
  return CMD_EXIT_OK;
}

static int
cmd_magi_controller_temperature_encode(cmd_node_t *cmd, stream_t *stream)
{
  int32_t temp = adc_get_temperature();
  return stream_error(stream_printf(stream, "%ld deg C\r\n", (long)temp)) ? 1
                                                                          : 0;
};

static cmd_node_t cmd_magi_controller_hostname = {
  .symbol = "hostname",
  .encode = cmd_magi_controller_hostname_encode,
  .decode = cmd_magi_controller_hostname_decode,
};

static cmd_node_t cmd_magi_controller_temperature = {
  .symbol = "temperature",
  .encode = &cmd_magi_controller_temperature_encode,
  .decode = NULL,
  .load = NULL,
  .save = NULL,
  .exec = NULL,
  .child = NULL,
  .next = &cmd_magi_controller_hostname,
};

static cmd_node_t cmd_magi_controller = {
  .symbol = "controller",
  .encode = NULL,
  .decode = NULL,
  .load = NULL,
  .save = NULL,
  .exec = NULL,
  .child = &cmd_magi_controller_temperature,
  .next = NULL,
};

static cmd_node_t cmd_root_instance = {
  .symbol = "magi",
  .encode = NULL,
  .decode = NULL,
  .load = NULL,
  .save = NULL,
  .exec = NULL,
  .child = &cmd_magi_controller,
  .next = NULL,
};

static cmd_node_t *cmd_root = &cmd_root_instance;

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
  CMD_ARGUMENT_SUFFIX,
} cmd_argument_state_t;

typedef struct
{
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
  stream_t *io;
  cmd_exec_t *exec;
  cmd_argument_state_t state;
  stream_status_t status;
  stream_t stream;
} cmd_argument_t;

static stream_status_t
cmd_line_stream_exchange(stream_t *stream,
                         uint8_t *read_data,
                         bool read,
                         uint8_t write_data,
                         bool write)
{
  cmd_line_t *line = container_of(stream, cmd_line_t, stream);
  stream_status_t result = 0;

  if (read)
    {
      if (line->read_count < line->write_count)
        {
          *read_data = line->line[line->read_count];
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
      result |= stream_putc(line->io, write_data);
    }
  return result;
}

static void
cmd_line_init(cmd_line_t *line, stream_t *stream)
{
  line->io = stream;
  line->write_count = 0;
  line->new_line = 0;
  line->read_count = 0;
  line->stream.exchange = cmd_line_stream_exchange;
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
      if (line->write_count >= CMD_LINE_MAX)
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
      line->line[line->write_count] = c;
      line->write_count++;
      uint8_t next = 0;
      bool read = true;
      bool write = true;
      do
        {
          result = line->io->exchange(line->io, &next, read, c, write);
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
                             uint8_t *read_data,
                             bool read,
                             uint8_t write_data,
                             bool write)
{
  cmd_argument_t *argument = container_of(stream, cmd_argument_t, stream);
  if (!read || argument->status != 0)
    {
      stream_status_t result
          = write ? stream_putc(argument->io, write_data) : 0;
      return result | (read ? argument->status : 0);
    }

  while (true)
    {
      uint8_t c = 0;
      stream_status_t result
          = argument->io->exchange(argument->io, &c, true, write_data, write);
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
      if (cmd_is_whitespace(c))
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
          *read_data = c;
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
}

static bool
cmd_symbol_equal(const char *symbol, uint8_t *input, uint32_t n)
{
  return strlen(symbol) == n && memcmp(symbol, input, n) == 0;
}

static cmd_symbol_result_t
cmd_read_symbol(cmd_exec_t *exec, stream_t *stream)
{
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

static cmd_node_t *
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
cmd_sm_next_symbol(cmd_exec_t *exec, stream_t *stream)
{
  cmd_symbol_result_t result = 0;
  result = cmd_read_symbol(exec, stream);
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
cmd_sm_error(cmd_exec_t *exec, stream_t *stream)
{
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
cmd_sm_walk(cmd_exec_t *exec, stream_t *stream)
{
  cmd_sm_next_symbol(exec, stream);
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
cmd_sm_get_key(cmd_exec_t *exec, stream_t *stream)
{
  cmd_sm_next_symbol(exec, stream);
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
cmd_sm_get(cmd_exec_t *exec, stream_t *stream)
{
  if (!exec->end)
    {
      cmd_sm_next_symbol(exec, stream);
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
  exec->exit_code = exec->cmd->encode(exec->cmd, stream);
  exec->state = CMD_STATE_DONE;
}

static void
cmd_sm_set_key(cmd_exec_t *exec, stream_t *stream)
{
  cmd_sm_next_symbol(exec, stream);
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
cmd_sm_set(cmd_exec_t *exec, stream_t *stream)
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
  cmd_argument_init(&argument, stream, exec);
  exec->exit_code = exec->cmd->decode(exec->cmd, &argument.stream);
  if (exec->state == CMD_STATE_SET)
    {
      exec->state = CMD_STATE_DONE;
    }
}

static void
cmd_sm_exec(cmd_exec_t *exec, stream_t *stream)
{
  if (exec->cmd->exec == NULL)
    {
      exec->state = CMD_STATE_ERROR;
      exec->error = CMD_ERROR_UNSUPPORTED_OPERATION;
      return;
    }
  exec->exit_code = exec->cmd->exec(exec->cmd, stream);
  exec->state = CMD_STATE_DONE;
}

static int
cmd_exec(stream_t *stream)
{
  cmd_exec_t exec = { 0 };

  while (true)
    {
      switch (exec.state)
        {
        case CMD_STATE_DONE:
          return exec.exit_code;
        case CMD_STATE_WALK:
          cmd_sm_walk(&exec, stream);
          break;
        case CMD_STATE_GET_KEY:
          cmd_sm_get_key(&exec, stream);
          break;
        case CMD_STATE_GET:
          cmd_sm_get(&exec, stream);
          break;
        case CMD_STATE_SET_KEY:
          cmd_sm_set_key(&exec, stream);
          break;
        case CMD_STATE_SET:
          cmd_sm_set(&exec, stream);
          break;
        case CMD_STATE_EXEC:
          cmd_sm_exec(&exec, stream);
          break;
        case CMD_STATE_ERROR:
          cmd_sm_error(&exec, stream);
          break;
        }
    }
}

void
cmd_shell(stream_t *stream)
{
  cmd_line_t cmd_line = { 0 };
  cmd_line_init(&cmd_line, stream);

  while (true)
    {
      stream_status_t result = 0;
      result = stream_printf(stream, "%s> ", cmd_hostname);
      if (stream_error(result))
        {
          return;
        }
      switch (cmd_line_readline(&cmd_line))
        {
        case CMD_LINE_OK:
          cmd_exec(&cmd_line.stream);
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
