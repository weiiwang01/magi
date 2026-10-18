#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <stdbool.h>

#define NETWORK_HOSTNAME_SIZE 26U
#define NETWORK_HOSTNAME_MAX 32U
#define NETWORK_ADDRESS_SIZE 50U

void network_init();
void network_exti_irq();

void network_get_hostname(char hostname[static NETWORK_HOSTNAME_MAX + 1]);
int network_set_hostname(const char *hostname);
void network_default_hostname(char hostname[static NETWORK_HOSTNAME_SIZE]);
bool network_get_ipv4_address(char value[static NETWORK_ADDRESS_SIZE]);
bool network_get_ipv6_address(char value[static NETWORK_ADDRESS_SIZE]);
bool
network_get_ipv6_link_local_address(char value[static NETWORK_ADDRESS_SIZE]);

#endif
