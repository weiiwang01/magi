//
// Created by weiiwang01 on 9/10/26.
//

#ifndef CMD_H
#define CMD_H

#include "stream.h"

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
  int (*decode)(cmd_node_t *cmd, stream_t *stream);
  int (*encode)(cmd_node_t *cmd, stream_t *stream);
  int (*load)(cmd_node_t *cmd, stream_t *stream);
  int (*save)(cmd_node_t *cmd, stream_t *stream);
  int (*exec)(cmd_node_t *cmd, stream_t *stream);
  cmd_node_t *child;
  cmd_node_t *next;
};

void cmd_shell(stream_t *stream);

#endif // CMD_H
