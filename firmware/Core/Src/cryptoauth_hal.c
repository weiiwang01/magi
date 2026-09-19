#include "cryptoauthlib.h"
#include "hal/atca_hal.h"

#include <errno.h>
#include <string.h>

#include "timeout.h"
#include "xpander.h"

static ATCA_STATUS
cryptoauth_transport_status(int error)
{
  switch (error)
    {
    case 0:
      return ATCA_SUCCESS;
    case EINVAL:
      return ATCA_BAD_PARAM;
    case ENODEV:
      return ATCA_NOT_INITIALIZED;
    case ENXIO:
      return ATCA_RX_NO_RESPONSE;
    case ETIMEDOUT:
      return ATCA_TIMEOUT;
    default:
      return ATCA_COMM_FAIL;
    }
}

ATCA_STATUS
hal_i2c_init(ATCAIface iface, ATCAIfaceCfg *cfg)
{
  if (iface == NULL || cfg == NULL || cfg->iface_type != ATCA_I2C_IFACE
      || cfg->devtype != ATECC608 || ATCA_IFACECFG_VALUE(cfg, atcai2c.bus) != 0
      || ATCA_IFACECFG_VALUE(cfg, atcai2c.baud) != 100000
      || (ATCA_IFACECFG_VALUE(cfg, atcai2c.address) & 1U) != 0)
    {
      return ATCA_BAD_PARAM;
    }
  uint8_t version;
  ATCA_STATUS status
      = cryptoauth_transport_status(xpander_get_version(&version));
  if (status == ATCA_SUCCESS && version != 1)
    {
      status = ATCA_COMM_FAIL;
    }
  if (status == ATCA_SUCCESS)
    {
      iface->hal_data = cfg;
    }
  return status;
}

ATCA_STATUS
hal_i2c_post_init(ATCAIface iface)
{
  return iface != NULL && iface->hal_data != NULL ? ATCA_SUCCESS
                                                  : ATCA_NOT_INITIALIZED;
}

ATCA_STATUS
hal_i2c_send(ATCAIface iface,
             uint8_t word_address,
             uint8_t *txdata,
             int txlength)
{
  if (iface == NULL || iface->mIfaceCFG == NULL || iface->hal_data == NULL)
    {
      return ATCA_NOT_INITIALIZED;
    }
  if (txlength < 0 || txlength > UINT8_MAX - 1
      || (txlength != 0 && txdata == NULL))
    {
      return ATCA_BAD_PARAM;
    }
  uint8_t data[UINT8_MAX];
  data[0] = word_address;
  if (txlength != 0)
    {
      memcpy(data + 1, txdata, (size_t)txlength);
    }
  return cryptoauth_transport_status(
      xpander_i2c_write(ATCA_IFACECFG_VALUE(iface->mIfaceCFG, atcai2c.address),
                        data,
                        (unsigned int)txlength + 1));
}

ATCA_STATUS
hal_i2c_receive(ATCAIface iface,
                uint8_t address,
                uint8_t *rxdata,
                uint16_t *rxlength)
{
  if (iface == NULL || iface->hal_data == NULL)
    {
      return ATCA_NOT_INITIALIZED;
    }
  if (rxdata == NULL || rxlength == NULL || *rxlength == 0
      || *rxlength > UINT8_MAX)
    {
      return ATCA_BAD_PARAM;
    }
  ATCA_STATUS status = cryptoauth_transport_status(
      xpander_i2c_read(address | 1U, rxdata, *rxlength));
  if (status != ATCA_SUCCESS)
    {
      *rxlength = 0;
    }
  return status;
}

ATCA_STATUS
hal_i2c_control(ATCAIface iface, uint8_t option, void *param, size_t paramlen)
{
  return iface != NULL ? ATCA_UNIMPLEMENTED : ATCA_BAD_PARAM;
}

ATCA_STATUS
hal_i2c_release(void *hal_data) { return ATCA_SUCCESS; }

void
hal_delay_us(uint32_t delay)
{
  timeout_sleep_us(delay);
}

void
hal_delay_ms(uint32_t delay)
{
  timeout_sleep_ms(delay + (delay != 0 ? portTICK_PERIOD_MS : 0));
}
