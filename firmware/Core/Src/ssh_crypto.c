#include "ssh_crypto.h"

#include <errno.h>
#include <string.h>
#include "crypto.h"
#include "cryptoauthlib.h"
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/coding.h>
#include <wolfssl/wolfcrypt/ecc.h>

#define SSH_HOST_SLOT 1U
#define SSH_AUTH_SLOT 10U

static const uint8_t ssh_key_prefix[] = {
  0,   0,   0,   19,  'e', 'c', 'd', 's', 'a', '-', 's', 'h', 'a', '2',
  '-', 'n', 'i', 's', 't', 'p', '2', '5', '6', 0,   0,   0,   8,   'n',
  'i', 's', 't', 'p', '2', '5', '6', 0,   0,   0,   65,  4,
};
static const uint8_t ssh_auth_marker[] = { 'S', 'S', 'H', 1 };

static int
ssh_host_public(uint8_t public_key[64], bool create)
{
  int result = crypto_lock();
  if (result != ATCA_SUCCESS)
    {
      return EIO;
    }
  atecc608_config_t config;
  result = atcab_read_config_zone((uint8_t *)&config);
  if (result == ATCA_SUCCESS
      && (config.LockConfig != 0 || config.LockValue != 0
          || (config.KeyConfig[SSH_HOST_SLOT] & 0x1dU) != 0x11U
          || (config.SlotConfig[SSH_HOST_SLOT] & 0x80U) == 0))
    {
      result = ATCA_BAD_PARAM;
    }
  if (result == ATCA_SUCCESS)
    {
      result = atcab_get_pubkey(SSH_HOST_SLOT, public_key);
      if (create && result == ATCA_EXECUTION_ERROR)
        {
          uint8_t validity[4] = { 0xff, 0xff, 0xff, 0xff };
          int status
              = atcab_info_base(INFO_MODE_KEY_VALID, SSH_HOST_SLOT, validity);
          if (status == ATCA_SUCCESS
              && (validity[0] | validity[1] | validity[2] | validity[3]) == 0)
            {
              result = atcab_genkey(SSH_HOST_SLOT, public_key);
            }
        }
    }
  crypto_unlock();
  return result == ATCA_SUCCESS ? 0 : EIO;
}

static int
ssh_encode_key(const uint8_t public_key[64], char output[SSH_KEY_BASE64_SIZE])
{
  uint8_t blob[SSH_KEY_BLOB_SIZE];
  memcpy(blob, ssh_key_prefix, sizeof(ssh_key_prefix));
  memcpy(blob + sizeof(ssh_key_prefix), public_key, 64);
  word32 length = SSH_KEY_BASE64_SIZE - 1;
  int result = Base64_Encode_NoNl(blob, sizeof(blob), (byte *)output, &length);
  if (result != 0)
    {
      return EIO;
    }
  output[length] = '\0';
  return 0;
}

static int
ssh_validate_point(const uint8_t public_key[64])
{
  if (crypto_lock() != ATCA_SUCCESS)
    {
      return EIO;
    }
  uint8_t point[65] = { 4 };
  memcpy(point + 1, public_key, 64);
  ecc_key key;
  int result = wc_ecc_init(&key);
  if (result == 0)
    {
      result
          = wc_ecc_import_x963_ex(point, sizeof(point), &key, ECC_SECP256R1);
      if (result == 0)
        {
          result = wc_ecc_check_key(&key);
        }
      wc_ecc_free(&key);
    }
  crypto_unlock();
  return result == 0 ? 0 : EINVAL;
}

static int
ssh_read_authorized(uint8_t public_key[64])
{
  uint8_t record[72];
  int result = atcab_read_bytes_zone(
      ATCA_ZONE_DATA, SSH_AUTH_SLOT, 0, record, sizeof(record));
  if (result != ATCA_SUCCESS)
    {
      return EIO;
    }
  if (memcmp(record, ssh_auth_marker, sizeof(ssh_auth_marker)) != 0)
    {
      return ENOENT;
    }
  memcpy(public_key, record + 4, 32);
  memcpy(public_key + 32, record + 40, 32);
  return 0;
}

int
ssh_get_host_key(char output[SSH_KEY_BASE64_SIZE])
{
  uint8_t public_key[64];
  int error = ssh_host_public(public_key, false);
  return error != 0 ? error : ssh_encode_key(public_key, output);
}

int
ssh_get_authorized_key(char output[SSH_KEY_BASE64_SIZE])
{
  uint8_t public_key[64];
  if (crypto_lock() != ATCA_SUCCESS)
    {
      return EIO;
    }
  int error = ssh_read_authorized(public_key);
  crypto_unlock();
  if (error == 0)
    {
      error = ssh_validate_point(public_key);
    }
  return error != 0 ? error : ssh_encode_key(public_key, output);
}

int
ssh_set_authorized_key(const char *input, size_t length)
{
  uint8_t blob[SSH_KEY_BLOB_SIZE];
  word32 size = sizeof(blob);
  if (input == NULL || length != SSH_KEY_BASE64_SIZE - 1
      || Base64_Decode((const byte *)input, (word32)length, blob, &size) != 0
      || size != sizeof(blob)
      || memcmp(blob, ssh_key_prefix, sizeof(ssh_key_prefix)) != 0)
    {
      return EINVAL;
    }
  int error = ssh_validate_point(blob + sizeof(ssh_key_prefix));
  if (error != 0)
    {
      return error;
    }
  char canonical[SSH_KEY_BASE64_SIZE];
  if (ssh_encode_key(blob + sizeof(ssh_key_prefix), canonical) != 0
      || memcmp(input, canonical, length) != 0)
    {
      return EINVAL;
    }
  if (crypto_lock() != ATCA_SUCCESS)
    {
      return EIO;
    }
  uint8_t record[72] = { 0 }, check[72];
  memcpy(record + 4, blob + sizeof(ssh_key_prefix), 32);
  memcpy(record + 40, blob + sizeof(ssh_key_prefix) + 32, 32);
  int result
      = atcab_write_bytes_zone(ATCA_ZONE_DATA, SSH_AUTH_SLOT, 0, record, 4);
  if (result == ATCA_SUCCESS)
    {
      result
          = atcab_read_bytes_zone(ATCA_ZONE_DATA, SSH_AUTH_SLOT, 0, check, 4);
      if (result == ATCA_SUCCESS && memcmp(check, record, 4) != 0)
        {
          result = ATCA_CHECKMAC_VERIFY_FAILED;
        }
    }
  if (result == ATCA_SUCCESS)
    {
      result = atcab_write_bytes_zone(
          ATCA_ZONE_DATA, SSH_AUTH_SLOT, 0, record, sizeof(record));
    }
  if (result == ATCA_SUCCESS)
    {
      result = atcab_read_bytes_zone(
          ATCA_ZONE_DATA, SSH_AUTH_SLOT, 0, check, sizeof(check));
      if (result == ATCA_SUCCESS && memcmp(check, record, sizeof(record)) != 0)
        {
          result = ATCA_CHECKMAC_VERIFY_FAILED;
        }
    }
  if (result == ATCA_SUCCESS)
    {
      result = atcab_write_bytes_zone(ATCA_ZONE_DATA,
                                      SSH_AUTH_SLOT,
                                      0,
                                      ssh_auth_marker,
                                      sizeof(ssh_auth_marker));
    }
  if (result == ATCA_SUCCESS)
    {
      uint8_t public_key[64];
      result = ssh_read_authorized(public_key);
      if (result == 0
          && memcmp(public_key, blob + sizeof(ssh_key_prefix), 64) != 0)
        {
          result = ATCA_CHECKMAC_VERIFY_FAILED;
        }
    }
  crypto_unlock();
  return result == ATCA_SUCCESS ? 0 : EIO;
}

int
ssh_clear_authorized_key()
{
  if (crypto_lock() != ATCA_SUCCESS)
    {
      return EIO;
    }
  const uint8_t invalid[4] = { 0 };
  uint8_t check[4];
  int result = atcab_write_bytes_zone(
      ATCA_ZONE_DATA, SSH_AUTH_SLOT, 0, invalid, sizeof(invalid));
  if (result == ATCA_SUCCESS)
    {
      result = atcab_read_bytes_zone(
          ATCA_ZONE_DATA, SSH_AUTH_SLOT, 0, check, sizeof(check));
      if (result == ATCA_SUCCESS && memcmp(check, invalid, sizeof(check)) != 0)
        {
          result = ATCA_CHECKMAC_VERIFY_FAILED;
        }
    }
  crypto_unlock();
  return result == ATCA_SUCCESS ? 0 : EIO;
}

int
ssh_authorize_key(const uint8_t *blob, size_t length)
{
  if (blob == NULL || length != SSH_KEY_BLOB_SIZE
      || memcmp(blob, ssh_key_prefix, sizeof(ssh_key_prefix)) != 0
      || crypto_lock() != ATCA_SUCCESS)
    {
      return 0;
    }
  uint8_t public_key[64];
  int error = ssh_read_authorized(public_key);
  crypto_unlock();
  return error == 0
         && memcmp(public_key, blob + sizeof(ssh_key_prefix), 64) == 0;
}

int
ssh_crypto_init(WOLFSSH_CTX *ctx)
{
  uint8_t public_key[64];
  if (ssh_host_public(public_key, true) != 0)
    {
      return EIO;
    }
  uint8_t der[91] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48,
    0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48,
    0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04,
  };
  memcpy(der + 27, public_key, sizeof(public_key));
  return wolfSSH_CTX_UsePrivateKey_buffer(
             ctx, der, sizeof(der), WOLFSSH_FORMAT_ASN1)
                 == WS_SUCCESS
             ? 0
             : EIO;
}
