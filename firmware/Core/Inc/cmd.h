//
// Created by weiiwang01 on 9/10/26.
//

#ifndef CMD_H
#define CMD_H

#include "stream.h"

#define CMD_SYMBOL_MAX 32
#define CMD_LEVEL_MAX 16

typedef enum
{
  CMD_EXIT_OK = 0,
  CMD_EXIT_MISUSE = 2,
  CMD_EXIT_IOERR = 74,
  CMD_EXIT_NOT_FOUND = 127,
  CMD_EXIT_INTERRUPT = 130,
} cmd_exit_code_t;

typedef struct cmd_node_t cmd_node_t;

struct cmd_node_t
{
  const char *symbol;
  bool dirty;
  int (*decode)(cmd_node_t *cmd, stream_t *stream);
  int (*encode)(cmd_node_t *cmd, stream_t *stream);
  int (*load)(cmd_node_t *cmd, stream_t *stream);
  int (*save)(cmd_node_t *cmd, stream_t *stream);
  int (*exec)(cmd_node_t *cmd, stream_t *stream);
  cmd_node_t *child;
  cmd_node_t *next;
};

extern cmd_node_t *cmd_root;

void cmd_shell(stream_t *stream);
int cmd_execute(stream_t *stream, const char *command);
cmd_node_t *
cmd_search_node(cmd_node_t *cmd, uint8_t *input, uint32_t input_len);

#endif // CMD_H
