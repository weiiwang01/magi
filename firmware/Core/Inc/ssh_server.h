#ifndef MAGI_SSH_SERVER_H
#define MAGI_SSH_SERVER_H

#include <stddef.h>
#include <stdint.h>

#define SSH_KEY_BASE64_SIZE 141U
#define SSH_KEY_BLOB_SIZE 104U

void ssh_server_init();
int ssh_get_host_key(char output[SSH_KEY_BASE64_SIZE]);
int ssh_get_authorized_key(char output[SSH_KEY_BASE64_SIZE]);
int ssh_set_authorized_key(const char *input, size_t length);
int ssh_clear_authorized_key();

#endif
