#ifndef SWITCH_H
#define SWITCH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  SWITCH_PROTOCOL_SGMII,
  SWITCH_PROTOCOL_HSGMII,
} switch_protocol_t;

typedef enum
{
  SWITCH_SPEED_AUTO,
  SWITCH_SPEED_10M,
  SWITCH_SPEED_100M,
  SWITCH_SPEED_1000M,
  SWITCH_SPEED_2500M,
} switch_speed_t;

typedef enum
{
  SWITCH_DUPLEX_HALF,
  SWITCH_DUPLEX_FULL,
} switch_duplex_t;

typedef struct
{
  bool link;
  switch_speed_t speed;
  bool full_duplex;
} switch_port_status_t;

int switch_init();
int switch_enable_sgmii(switch_protocol_t protocol,
                        switch_speed_t speed,
                        switch_duplex_t duplex);
int switch_disable_sgmii();
int switch_get_port_status(uint8_t port, switch_port_status_t *status);

#endif // SWITCH_H
