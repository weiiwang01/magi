#ifndef MAGI_SSH_CRYPTO_H
#define MAGI_SSH_CRYPTO_H

#include "ssh_server.h"
#include <wolfssh/ssh.h>

int ssh_crypto_init(WOLFSSH_CTX *ctx);
int ssh_authorize_key(const uint8_t *blob, size_t length);

#endif
