//
// Created by weiiwang01 on 10/18/26.
//

#ifndef CONFIG_H
#define CONFIG_H

#include "stream.h"

int config_load();
int config_save();
int config_reset();
stream_status_t config_read_vint(stream_t *stream, uint32_t *read);
stream_status_t config_write_vint(stream_t *stream, uint32_t write);

#endif // CONFIG_H
