#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>

#include <lwip/err.h>
#include <lwip/ip4_addr.h>
#include <lwip/ip6_addr.h>
#include <lwip/ethip6.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#if !NO_SYS
#include <lwip/tcpip.h>
#endif

#include "ch390.h"

#define CH390_RCR_ACTIVE (RCR_DIS_CRC | RCR_RXEN)
#define CH390_TX_WAIT_RETRIES 100U

static bool
ch390_device_valid(const ch390_t *dev)
{
  return dev != NULL && dev->chip != NULL && dev->chip->reset != NULL
         && dev->chip->spi_select != NULL && dev->chip->spi_read != NULL
         && dev->chip->spi_write != NULL && dev->chip->lock != NULL
         && dev->chip->unlock != NULL && dev->chip->sleep != NULL;
}

static uint32_t
ch390_crc32_le(const uint32_t previous_crc32,
               const uint8_t *data,
               size_t length)
{
  // https://github.com/stbrumme/crc32/blob/master/Crc32.cpp
  uint32_t crc = previous_crc32;
  const uint8_t *cursor = data;

  static const uint32_t crc32_lookup16[16]
      = { 0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
          0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
          0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
          0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C };

  while (length-- != 0)
    {
      crc = crc32_lookup16[(crc ^ *cursor) & 0x0F] ^ (crc >> 4);
      crc = crc32_lookup16[(crc ^ (*cursor >> 4)) & 0x0F] ^ (crc >> 4);
      cursor++;
    }

  return crc;
}

static void
ch390_mac_filter_init(ch390_mac_filter_t *filter)
{
  memset(filter, 0, sizeof(ch390_mac_filter_t));
}

static uint8_t
ch390_mac_filter_index(const uint8_t mac[6])
{
  uint32_t crc = ~ch390_crc32_le(~0u, mac, 6);
  uint8_t index = 0;

  for (unsigned int bit = 0; bit < 6; ++bit)
    {
      index = (uint8_t)((index << 1) | (crc & 1U));
      crc >>= 1;
    }
  return index;
}

static int
ch390_mac_filter_add(ch390_mac_filter_t *filter, const uint8_t mac[static 6])
{
  uint8_t *bucket = &filter->hash_bucket[ch390_mac_filter_index(mac)];
  if (*bucket < 255)
    {
      *bucket += 1;
    }
  return 0;
}

static int
ch390_mac_filter_remove(ch390_mac_filter_t *filter,
                        const uint8_t mac[static 6])
{
  uint8_t *bucket = &filter->hash_bucket[ch390_mac_filter_index(mac)];
  if (*bucket > 0)
    {
      *bucket -= 1;
    }
  return 0;
}

static int
ch390_mac_filter_mar(const ch390_mac_filter_t *filter, uint8_t mar[static 8])
{
  for (int i = 0; i < 8; i++)
    {
      uint8_t reg = i == 7 ? 0x80 : 0x00;
      for (int j = 0; j < 8; j++)
        {
          if (filter->hash_bucket[i * 8 + j] > 0)
            {
              reg |= (uint8_t)(1u << j);
            }
        }
      mar[i] = reg;
    }
  return 0;
}

static int
ch390_spi_read(ch390_t *dev, uint8_t reg, uint8_t *data, unsigned int len)
{
  int rtn = 0;
  int err = 0;

  if (data == NULL || len == 0)
    return EINVAL;

  err = dev->chip->spi_select(dev->chip, true);
  if (err)
    goto deselect;

  reg |= OPC_REG_R;
  err = dev->chip->spi_write(dev->chip, &reg, 1);
  if (err)
    goto deselect;

  err = dev->chip->spi_read(dev->chip, data, len);
  if (err)
    goto deselect;

deselect:
  rtn = dev->chip->spi_select(dev->chip, false);
  if (err)
    rtn = err;
  return rtn;
}

static int
ch390_spi_write(ch390_t *dev,
                uint8_t reg,
                const uint8_t *data,
                unsigned int len)
{
  int err = 0;
  int rtn = 0;

  if (data == NULL || len == 0)
    return EINVAL;

  err = dev->chip->spi_select(dev->chip, true);
  if (err)
    goto deselect;

  reg |= OPC_REG_W;
  err = dev->chip->spi_write(dev->chip, &reg, 1);
  if (err)
    goto deselect;

  err = dev->chip->spi_write(dev->chip, data, len);
  if (err)
    goto deselect;

deselect:
  rtn = dev->chip->spi_select(dev->chip, false);
  if (err)
    rtn = err;
  return rtn;
}

static int
ch390_spi_discard(ch390_t *dev, uint8_t reg, unsigned int len)
{
  int err = 0;
  int rtn = 0;

  uint8_t buf[64];

  if (len == 0)
    return EINVAL;

  err = dev->chip->spi_select(dev->chip, true);
  if (err)
    goto deselect;

  reg |= OPC_REG_R;
  err = dev->chip->spi_write(dev->chip, &reg, 1);
  if (err)
    goto deselect;

  while (len > 0)
    {
      err = dev->chip->spi_read(dev->chip, buf, len > 64 ? 64 : len);
      if (err)
        goto deselect;
      len -= len > 64 ? 64 : len;
    }

deselect:
  rtn = dev->chip->spi_select(dev->chip, false);
  if (err)
    {
      rtn = err;
    }
  return rtn;
}

static int
ch390_spi_read_pbuf(ch390_t *dev, uint8_t reg, struct pbuf *buf)
{
  int rtn = 0;
  int err = 0;
  int len = 0;
  int remaining;
  if (buf == NULL || buf->tot_len == 0)
    {
      return EINVAL;
    }
  remaining = buf->tot_len;

  err = dev->chip->spi_select(dev->chip, true);
  if (err)
    goto deselect;

  reg |= OPC_REG_R;
  err = dev->chip->spi_write(dev->chip, &reg, 1);
  if (err)
    goto deselect;

  while (remaining > 0)
    {
      if (buf == NULL)
        {
          err = EINVAL;
          goto deselect;
        }
      len = buf->len;
      if (len == 0)
        {
          buf = buf->next;
          continue;
        }
      if (len > remaining)
        {
          err = EINVAL;
          goto deselect;
        }
      err = dev->chip->spi_read(dev->chip, buf->payload, len);
      if (err)
        goto deselect;
      remaining -= len;
      buf = buf->next;
    }

deselect:
  rtn = dev->chip->spi_select(dev->chip, false);
  if (err)
    {
      rtn = err;
    }
  return rtn;
}

static int
ch390_spi_write_pbuf(ch390_t *dev, uint8_t reg, const struct pbuf *buf)
{
  int rtn = 0;
  int err = 0;
  int len = 0;
  int remaining;

  if (buf == NULL || buf->tot_len == 0)
    {
      return EINVAL;
    }
  remaining = buf->tot_len;

  err = dev->chip->spi_select(dev->chip, true);
  if (err)
    goto deselect;

  reg |= OPC_REG_W;
  err = dev->chip->spi_write(dev->chip, &reg, 1);
  if (err)
    goto deselect;

  while (remaining > 0)
    {
      if (buf == NULL)
        {
          err = EINVAL;
          goto deselect;
        }
      len = buf->len;
      if (len == 0)
        {
          buf = buf->next;
          continue;
        }
      if (len > remaining)
        {
          err = EINVAL;
          goto deselect;
        }
      err = dev->chip->spi_write(dev->chip, buf->payload, len);
      if (err)
        goto deselect;
      remaining -= len;
      buf = buf->next;
    }

deselect:
  rtn = dev->chip->spi_select(dev->chip, false);
  if (err)
    {
      rtn = err;
    }
  return rtn;
}

static int
ch390_read_reg(ch390_t *dev, uint8_t reg, uint8_t *data)
{
  return ch390_spi_read(dev, reg, data, 1);
}

static int
ch390_write_reg(ch390_t *dev, uint8_t reg, uint8_t data)
{
  int err = 0;
  int rtn = 0;

  err = dev->chip->spi_select(dev->chip, true);
  if (err)
    goto deselect;

  reg |= OPC_REG_W;
  uint8_t write[2] = { reg, data };
  err = dev->chip->spi_write(dev->chip, write, 2);
  if (err)
    goto deselect;

deselect:
  rtn = dev->chip->spi_select(dev->chip, false);
  if (err)
    {
      rtn = err;
    }
  return rtn;
}

static int
ch390_read_regs(ch390_t *dev, uint8_t reg, uint8_t *data, unsigned int len)
{
  int rtn;

  for (unsigned int i = 0; i < len; ++i)
    {
      rtn = ch390_read_reg(dev, (uint8_t)(reg + i), &data[i]);
      if (rtn)
        return rtn;
    }
  return 0;
}

static int
ch390_write_regs(ch390_t *dev,
                 uint8_t reg,
                 const uint8_t *data,
                 unsigned int len)
{
  int rtn;

  for (unsigned int i = 0; i < len; ++i)
    {
      rtn = ch390_write_reg(dev, (uint8_t)(reg + i), data[i]);
      if (rtn)
        return rtn;
    }
  return 0;
}

static int
ch390_update_mar(ch390_t *dev)
{
  uint8_t mar[8] = { 0 };
  ch390_mac_filter_mar(&dev->mac_filter, mar);
  return ch390_write_regs(dev, CH390_MAR, mar, 8);
}

static int
ch390_get_hwaddr(ch390_t *dev, uint8_t hwaddr[static 6])
{
  return ch390_read_regs(dev, CH390_PAR, hwaddr, 6);
}

static int
ch390_get_link(ch390_t *dev, bool *up)
{
  int rtn = 0;
  uint8_t reg = 0;

  rtn = ch390_read_reg(dev, CH390_NSR, &reg);
  if (rtn)
    return rtn;

  *up = reg & NSR_LINKST ? true : false;
  return 0;
}

static int
ch390_apply_link_state(ch390_t *dev)
{
  bool link_up;
  int rtn;

  dev->chip->lock(dev->chip);
  rtn = ch390_get_link(dev, &link_up);
  dev->chip->unlock(dev->chip);
  if (rtn)
    return rtn;

  if (link_up)
    netif_set_link_up(dev->netif);
  else
    netif_set_link_down(dev->netif);

  return 0;
}

#if !NO_SYS
static void
ch390_link_callback(void *ctx)
{
  ch390_apply_link_state(ctx);
}

static int
ch390_notify_link_change(ch390_t *dev)
{
  err_t err = tcpip_try_callback(ch390_link_callback, dev);

  if (err == ERR_OK)
    return 0;
  return err == ERR_MEM ? ENOMEM : EIO;
}
#else
static int
ch390_notify_link_change(ch390_t *dev)
{
  return ch390_apply_link_state(dev);
}
#endif

static int
ch390_reset_rx(ch390_t *dev)
{
  int rtn;

  rtn = ch390_write_reg(dev, CH390_RCR, 0);
  if (rtn)
    return rtn;

  rtn = ch390_write_reg(dev, CH390_MPTRCR, 0x01);
  if (rtn)
    return rtn;

  rtn = ch390_write_reg(dev, CH390_MRRH, 0x0c);
  if (rtn)
    return rtn;

  dev->chip->sleep(dev->chip, 1);

  return ch390_write_reg(dev, CH390_RCR, CH390_RCR_ACTIVE);
}

static int
ch390_receive_nolock(ch390_t *dev, struct pbuf **packet)
{
  int rtn = 0;
  uint16_t len = 0;
  uint8_t header[4] = { 0 };
  struct pbuf *buf;

  if (packet == NULL || *packet != NULL)
    return EINVAL;

  while (true)
    {
      rtn = ch390_read_reg(dev, CH390_MRCMDX, header);
      if (rtn)
        return rtn;

      rtn = ch390_read_reg(dev, CH390_MRCMDX, header);
      if (rtn)
        return rtn;

      if (header[0] & CH390_PKT_ERR)
        return ch390_reset_rx(dev);

      if (header[0] != CH390_PKT_RDY)
        return 0; /* no packets */

      rtn = ch390_spi_read(dev, CH390_MRCMD, header, 4);
      if (rtn)
        return rtn;

      len = (u16_t)(header[2] | ((u16_t)header[3] << 8));

      if (len <= 4 || len > CH390_PKT_MAX)
        return ch390_reset_rx(dev);

      if (header[1] & RSR_ERR_BITS)
        {
          rtn = ch390_spi_discard(dev, CH390_MRCMD, len);
          if (rtn)
            return rtn;
          continue;
        }

      break;
    }

  uint16_t frame_len = (uint16_t)(len - 4);

  buf = pbuf_alloc(PBUF_RAW, frame_len, PBUF_POOL);
  if (buf == NULL)
    {
      rtn = ch390_spi_discard(dev, CH390_MRCMD, len);
      return rtn ? rtn : ENOMEM;
    }

  rtn = ch390_spi_read_pbuf(dev, CH390_MRCMD, buf);
  if (rtn)
    {
      pbuf_free(buf);
      return rtn;
    }

  uint8_t crc[4];
  rtn = ch390_spi_read(dev, CH390_MRCMD, crc, 4);
  if (rtn)
    {
      pbuf_free(buf);
      return rtn;
    }

  *packet = buf;
  return 0;
}

int
ch390_receive(ch390_t *dev, struct pbuf **packet)
{
  int rtn = 0;

  if (packet == NULL)
    return EINVAL;

  dev->chip->lock(dev->chip);
  rtn = ch390_receive_nolock(dev, packet);
  dev->chip->unlock(dev->chip);
  return rtn;
}

int
ch390_transmit(ch390_t *dev, struct pbuf *packet)
{
  int ret = 0;
  uint8_t reg = 0;
  unsigned int retries = 0;

  if (packet == NULL || packet->tot_len == 0
      || packet->tot_len > CH390_PKT_MAX)
    return EINVAL;

  dev->chip->lock(dev->chip);

  do
    {
      ret = ch390_read_reg(dev, CH390_TCR, &reg);
      if (ret)
        goto unlock;
      if (reg & TCR_TXREQ)
        {
          if (retries >= CH390_TX_WAIT_RETRIES)
            {
              ret = ETIMEDOUT;
              goto unlock;
            }
          retries += 1;
          dev->chip->sleep(dev->chip, 1);
        }
    }
  while (reg & TCR_TXREQ);

  ret = ch390_spi_write_pbuf(dev, CH390_MWCMD, packet);
  if (ret)
    goto unlock;

  ret = ch390_write_reg(dev, CH390_TXPLL, (uint8_t)(packet->tot_len & 0xffu));
  if (ret)
    goto unlock;

  ret = ch390_write_reg(dev, CH390_TXPLH, (uint8_t)(packet->tot_len >> 8));
  if (ret)
    goto unlock;

  ret = ch390_read_reg(dev, CH390_TCR, &reg);
  if (ret)
    goto unlock;

  ret = ch390_write_reg(dev, CH390_TCR, reg | TCR_TXREQ);
  if (ret)
    goto unlock;

unlock:
  dev->chip->unlock(dev->chip);
  return ret;
}

static err_t
ch390_change_mac_filter(ch390_t *dev,
                        const uint8_t mac[static 6],
                        enum netif_mac_filter_action action)
{
  int rtn;

  if (action == NETIF_ADD_MAC_FILTER)
    rtn = ch390_mac_filter_add(&dev->mac_filter, mac);
  else if (action == NETIF_DEL_MAC_FILTER)
    rtn = ch390_mac_filter_remove(&dev->mac_filter, mac);
  else
    rtn = EINVAL;

  if (rtn)
    return rtn == EOVERFLOW ? ERR_MEM : ERR_ARG;

  rtn = ch390_update_mar(dev);
  if (rtn)
    {
      if (action == NETIF_ADD_MAC_FILTER)
        (void)ch390_mac_filter_remove(&dev->mac_filter, mac);
      else
        (void)ch390_mac_filter_add(&dev->mac_filter, mac);
    }

  return rtn ? ERR_IF : ERR_OK;
}

static void
ch390_igmp_mac_address(const ip4_addr_t *group, uint8_t mac[static 6])
{
  mac[0] = 0x01;
  mac[1] = 0x00;
  mac[2] = 0x5e;
  mac[3] = ip4_addr2(group) & 0x7f;
  mac[4] = ip4_addr3(group);
  mac[5] = ip4_addr4(group);
}

static err_t
ch390_igmp_mac_filter(struct netif *netif,
                      const ip4_addr_t *group,
                      enum netif_mac_filter_action action)
{
  uint8_t mac[6];
  ch390_t *dev = (ch390_t *)netif->state;
  err_t rtn = 0;

  dev->chip->lock(dev->chip);

  ch390_igmp_mac_address(group, mac);
  rtn = ch390_change_mac_filter(dev, mac, action);

  dev->chip->unlock(dev->chip);
  return rtn;
}

#if LWIP_IPV6 && LWIP_IPV6_MLD
static void
ch390_mld_mac_address(const ip6_addr_t *group, uint8_t mac[static 6])
{
  uint16_t block7 = IP6_ADDR_BLOCK7(group);
  uint16_t block8 = IP6_ADDR_BLOCK8(group);

  mac[0] = 0x33;
  mac[1] = 0x33;
  mac[2] = (uint8_t)(block7 >> 8);
  mac[3] = (uint8_t)block7;
  mac[4] = (uint8_t)(block8 >> 8);
  mac[5] = (uint8_t)block8;
}

static err_t
ch390_mld_mac_filter(struct netif *netif,
                     const ip6_addr_t *group,
                     enum netif_mac_filter_action action)
{
  uint8_t mac[6];
  ch390_t *dev = (ch390_t *)netif->state;
  err_t rtn = 0;

  dev->chip->lock(dev->chip);

  ch390_mld_mac_address(group, mac);
  rtn = ch390_change_mac_filter(dev, mac, action);

  dev->chip->unlock(dev->chip);
  return rtn;
}
#endif

static err_t
ch390_linkoutput(struct netif *netif, struct pbuf *p)
{
  ch390_t *dev = (ch390_t *)netif->state;

  if (ch390_transmit(dev, p))
    {
      return ERR_IF;
    }

  return ERR_OK;
}

static int
ch390_enable_irq(ch390_t *dev)
{
  uint8_t reg = IMR_PAR | IMR_LNKCHGI | IMR_PTI | IMR_PRI;
  return ch390_spi_write(dev, CH390_IMR, &reg, 1);
}

static int
ch390_disable_irq(ch390_t *dev)
{
  uint8_t reg = IMR_PAR;
  return ch390_spi_write(dev, CH390_IMR, &reg, 1);
}

static int
ch390_read_clear_irq(ch390_t *dev, uint8_t *isr)
{
  int rtn = 0;

  rtn = ch390_read_reg(dev, CH390_ISR, isr);
  if (rtn)
    return rtn;

  return ch390_write_reg(dev, CH390_ISR, *isr & ISR_CLR_INT);
}

int
ch390_irq_handler(ch390_t *dev)
{
  uint8_t isr = 0;
  int rtn = 0;
  int enable_rtn;
  struct pbuf *packet = NULL;
  err_t input_rtn;

  dev->chip->lock(dev->chip);

  rtn = ch390_disable_irq(dev);
  if (rtn)
    goto out;

  rtn = ch390_read_clear_irq(dev, &isr);
  if (rtn)
    goto out;

  dev->link_change |= (isr & ISR_LNKCHG) != 0;

  // Don't know if ISR_PR is edge or level triggered, treat it as edge
  // triggered for safe if (isr & ISR_PR)
  {
    while (true)
      {
        packet = NULL;
        rtn = ch390_receive_nolock(dev, &packet);
        if (rtn)
          goto out;
        if (packet == NULL)
          break;

#if NO_SYS || LWIP_TCPIP_CORE_LOCKING_INPUT
        dev->chip->unlock(dev->chip);
#endif
        input_rtn = dev->netif->input(packet, dev->netif);
        if (input_rtn != ERR_OK)
          pbuf_free(packet);
#if NO_SYS || LWIP_TCPIP_CORE_LOCKING_INPUT
        dev->chip->lock(dev->chip);
#endif

        if (input_rtn != ERR_OK)
          {
            rtn = EIO;
            goto out;
          }
      }
  }

out:
  enable_rtn = ch390_enable_irq(dev);
  if (!rtn)
    rtn = enable_rtn;
  dev->chip->unlock(dev->chip);

  if (dev->link_change)
    {
      int link_rtn = ch390_notify_link_change(dev);
      if (!link_rtn)
        dev->link_change = false;
      if (!rtn)
        rtn = link_rtn;
    }

  return rtn;
}

static int
ch390_reset(ch390_t *dev)
{
  int rtn = dev->chip->reset(dev->chip);
  if (rtn)
    return rtn;

  return ch390_write_reg(dev, CH390_NCR, NCR_RST);
}

static int
ch390_restart_locked(ch390_t *dev)
{
  int rtn = 0;

  rtn = ch390_reset(dev);
  if (rtn)
    {
      return rtn;
    }

  rtn = ch390_write_reg(dev, CH390_GPR, 0);
  if (rtn)
    return rtn;

  dev->chip->sleep(dev->chip, 1);

  rtn = ch390_update_mar(dev);
  if (rtn)
    return rtn;

  rtn = ch390_write_reg(dev, CH390_RCR, CH390_RCR_ACTIVE);
  if (rtn)
    return rtn;

  uint8_t isr;

  rtn = ch390_read_clear_irq(dev, &isr);
  if (rtn)
    return rtn;

  return ch390_enable_irq(dev);
}

static int
ch390_stop_locked(ch390_t *dev)
{
  int rtn = 0;

  rtn = ch390_disable_irq(dev);
  if (rtn)
    return rtn;

  rtn = ch390_write_reg(dev, CH390_GPR, GPR_PHYPD);
  if (rtn)
    return rtn;

  return ch390_write_reg(dev, CH390_RCR, RCR_DIS_CRC);
}

int
ch390_init(ch390_t *dev, struct netif *netif)
{
  int rtn = 0;
  bool link_up = false;
#if LWIP_IPV6 && LWIP_IPV6_MLD
  uint8_t allnodes_mac[6];
  ip6_addr_t ip6_allnodes_ll;
#endif

  if (!ch390_device_valid(dev) || netif == NULL)
    return EINVAL;

  ch390_mac_filter_init(&dev->mac_filter);
  dev->link_change = false;
  netif->hwaddr_len = 6;
  netif->state = dev;
  dev->netif = netif;
  netif->name[0] = 'c';
  netif->name[1] = 'h';

  netif->mtu = 1500;
  netif->flags
      = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
  netif->linkoutput = ch390_linkoutput;

#if LWIP_IPV4
  netif->output = etharp_output;
#endif

#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
  netif->mtu6 = 1500;
#endif

#if LWIP_IPV4 && LWIP_IGMP
  netif->flags |= NETIF_FLAG_IGMP;
  netif_set_igmp_mac_filter(netif, ch390_igmp_mac_filter);
#endif

#if LWIP_IPV6 && LWIP_IPV6_MLD
  netif->flags |= NETIF_FLAG_MLD6;
  netif_set_mld_mac_filter(netif, ch390_mld_mac_filter);
  ip6_addr_set_allnodes_linklocal(&ip6_allnodes_ll);
  ch390_mld_mac_address(&ip6_allnodes_ll, allnodes_mac);
  rtn = ch390_mac_filter_add(&dev->mac_filter, allnodes_mac);
  if (rtn)
    return rtn;
#endif

  dev->chip->lock(dev->chip);

  rtn = ch390_restart_locked(dev);
  if (rtn)
    goto unlock;

  rtn = ch390_get_hwaddr(dev, netif->hwaddr);
  if (rtn)
    goto unlock;

  rtn = ch390_get_link(dev, &link_up);
  if (rtn)
    goto unlock;

  if (link_up)
    netif->flags |= NETIF_FLAG_LINK_UP;
  else
    netif->flags &= (u8_t)~NETIF_FLAG_LINK_UP;

unlock:
  if (rtn)
    {
      ch390_stop_locked(dev);
    }
  dev->chip->unlock(dev->chip);
  return rtn;
}
