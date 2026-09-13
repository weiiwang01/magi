//
// Created by weiiwang01 on 8/16/26.
//

#ifndef CH390_LINUX_CH390_H
#define CH390_LINUX_CH390_H

#include <inttypes.h>
#include <stdbool.h>

#include <lwip/pbuf.h>
#include <lwip/netif.h>

/*
 * Register definition
 */

#define CH390_NCR 0x00
#define NCR_WAKEEN (1 << 6)  /* Enable wakeup function */
#define NCR_FDX (1 << 3)     /* Duplex mode of the internal PHY */
#define NCR_LBK_MAC (1 << 1) /* MAC loop-back */
#define NCR_RST (1 << 0)     /* Softwate reset */
#define CH390_NSR 0x01
#define NSR_SPEED (1 << 7)  /* Speed of internal PHY */
#define NSR_LINKST (1 << 6) /* Link status of internal PHY */
#define NSR_WAKEST (1 << 5) /* Wakeup event status */
#define NSR_TX2END (1 << 3) /* Tx packet B complete status */
#define NSR_TX1END (1 << 2) /* Tx packet A complete status */
#define NSR_RXOV (1 << 1)   /* Rx fifo overflow */
#define NSR_RXRDY (1 << 0)
#define CH390_TCR 0x02
#define TCR_TJDIS (1 << 6)    /* Transmit jabber timer */
#define TCR_PAD_DIS2 (1 << 4) /* PAD appends for packet B */
#define TCR_CRC_DIS2 (1 << 3) /* CRC appends for packet B */
#define TCR_PAD_DIS1 (1 << 2) /* PAD appends for packet A */
#define TCR_CRC_DIS1 (1 << 1) /* CRC appends for packet A */
#define TCR_TXREQ (1 << 0)    /* Tx request */
#define CH390_TSRA 0x03
#define CH390_TSRB 0x04
#define TSR_TJTO (1 << 7) /* Transmit jabber time out */
#define TSR_LC (1 << 6)   /* Loss of carrier */
#define TSR_NC (1 << 5)   /* No carrier */
#define TSR_LCOL (1 << 4) /* Late collision */
#define TSR_COL (1 << 3)  /* Collision packet */
#define TSR_EC (1 << 2)   /* Excessive collision */
#define CH390_RCR 0x05
#define RCR_DEFAULT 0x00     /* Default settings */
#define RCR_WTDIS (1 << 6)   /* Disable 2048 bytes watch dog */
#define RCR_DIS_CRC (1 << 4) /* Discard CRC error packet */
#define RCR_ALL (1 << 3)     /* Pass all multicast */
#define RCR_RUNT (1 << 2)    /* Pass runt packet */
#define RCR_PRMSC (1 << 1)   /* Promiscuous mode */
#define RCR_RXEN (1 << 0)    /* Enable RX */
#define CH390_RSR 0x06
#define RSR_RF (1 << 7)   /* Rnt frame */
#define RSR_MF (1 << 6)   /* Multicast frame */
#define RSR_LCS (1 << 5)  /* Late collision seen */
#define RSR_RWTO (1 << 4) /* Receive watchdog time-out */
#define RSR_PLE (1 << 3)  /* Physical layer error */
#define RSR_AE (1 << 2)   /* Alignment error */
#define RSR_CE (1 << 1)   /* CRC error */
#define RSR_FOE (1 << 0)  /* FIFO overflow error */
#define RSR_ERR_BITS                                                          \
  (RSR_RF | RSR_LCS | RSR_RWTO | RSR_PLE | RSR_AE | RSR_CE | RSR_FOE)
#define CH390_ROCR 0x07
#define CH390_BPTR 0x08
#define CH390_FCTR 0x09
#define FCTR_HWOT(ot) ((ot & 0xf) << 4)
#define FCTR_LWOT(ot) (ot & 0xf)
#define CH390_FCR 0x0A
#define FCR_TXP0 (1 << 7)  /* Force TX Pause Packetwith 0000H */
#define FCR_TXPF (1 << 6)  /* Force TX PausePacketwith FFFFH */
#define FCR_TXPEN (1 << 5) /* TX Pause Packet Enable */
#define FCR_BKPA (1 << 4)  /* Back Pressure Mode */
#define FCR_BKPM (1 << 3)  /* Back Pressure Mode */
#define FCR_RXPS (1 << 2)  /* RX Pause Packet Status,Latch andReadClearly */
#define FCR_RXPCS (1 << 1) /* RX Pause Packet Current Status */
#define FCR_FLCE (1 << 0)  /* Flow Control Enable */
#define FCR_RXTX_BITS (FCR_TXPEN | FCR_BKPM | FCR_FLCE)
#define CH390_EPCR 0x0B
#define EPCR_REEP (1 << 5)  /* Reload EEPROM */
#define EPCR_WEP (1 << 4)   /* Write EEPROM enable */
#define EPCR_EPOS (1 << 3)  /* EEPROM or PHY operation select */
#define EPCR_ERPRR (1 << 2) /* EEPROM or PHY read command */
#define EPCR_ERPRW (1 << 1) /* EEPROM or PHY write command */
#define EPCR_ERRE (1 << 0)  /* EEPROM or PHY access status */
#define CH390_EPAR 0x0C
#define CH390_EPDRL 0x0D
#define CH390_EPDRH 0x0E
#define CH390_WCR 0x0F
#define WCR_LINKEN (1 << 5)   /* Link status change wakeup */
#define WCR_SAMPLEEN (1 << 4) /* Sample frame wakeup */
#define WCR_MAGICEN (1 << 3)  /* Magic packet wakeup */
#define WCR_LINKST (1 << 2)   /* Link status change event */
#define WCR_SAMPLEST (1 << 1) /* Sample frame event */
#define WCR_MAGICST (1 << 0)  /* Magic packet event */
#define CH390_PAR 0x10
#define CH390_MAR 0x16
#define CH390_GPCR 0x1E
#define CH390_GPR 0x1F
#define GPR_PHYPD (1 << 0) /* PHY power down */
#define CH390_TRPAL 0x22
#define CH390_TRPAH 0x23
#define CH390_RWPAL 0x24
#define CH390_RWPAH 0x25
#define CH390_VIDL 0x28
#define CH390_VIDH 0x29
#define CH390_PIDL 0x2A
#define CH390_PIDH 0x2B
#define CH390_CHIPR 0x2C
#define CH390_TCR2 0x2D
#define CH390_ATCR 0x30
#define CH390_TCSCR 0x31
#define TCSCR_ALL 0x1F
#define TCSCR_IPv6TCPCSE (1 << 4) /* IPv6 TCP checksum generation */
#define TCSCR_IPv6UDPCSE (1 << 3) /* IPv6 UDP checksum generation */
#define TCSCR_UDPCSE (1 << 2)     /* UDP checksum generation */
#define TCSCR_TCPCSE (1 << 1)     /* TCP checksum generation */
#define TCSCR_IPCSE (1 << 0)      /* IP checksum generation */
#define CH390_RCSCSR 0x32
#define RCSCSR_UDPS (1 << 7)  /* UDP checksum status */
#define RCSCSR_TCPS (1 << 6)  /* TCP checksum status */
#define RCSCSR_IPS (1 << 5)   /* IP checksum status */
#define RCSCSR_UDPP (1 << 4)  /* UDP packet of current received packet */
#define RCSCSR_TCPP (1 << 3)  /* TCP packet of current received packet */
#define RCSCSR_IPP (1 << 2)   /* IP packet of current received packet */
#define RCSCSR_RCSEN (1 << 1) /* Receive checksum checking enable */
#define RCSCSR_DCSE (1 << 0)  /* Discard checksum error packet */
#define CH390_MPAR 0x33
#define CH390_SBCR 0x38
#define CH390_INTCR 0x39
#define INCR_TYPE_OD 0x02
#define INCR_TYPE_PP 0x00
#define INCR_POL_L 0x01
#define INCR_POL_H 0x00
#define CH390_ALNCR 0x4A
#define CH390_SCCR 0x50
#define CH390_RSCCR 0x51
#define CH390_RLENCR 0x52
#define CH390_BCASTCR 0x53
#define CH390_INTCKCR 0x54
#define CH390_MPTRCR 0x55
#define CH390_MLEDCR 0x57
#define MLEDCR_LED_MOD3 (1 << 7)
#define MLEDCR_LED_TYPE1 (1 << 1)
#define MLEDCR_LED_TYPE0 (1 << 0)
#define MLEDCR_LED_MOD1 (MLEDCR_LED_MOD3 | MLEDCR_LED_TYPE0)
#define CH390_MRCMDX 0x70
#define CH390_MRCMDX1 0x71
#define CH390_MRCMD 0x72
#define CH390_MRRL 0x74
#define CH390_MRRH 0x75
#define CH390_MWCMDX 0x76
#define CH390_MWCMD 0x78
#define CH390_MWRL 0x7A
#define CH390_MWRH 0x7B
#define CH390_TXPLL 0x7C
#define CH390_TXPLH 0x7D
#define CH390_ISR 0x7E
#define ISR_IOMODE (1 << 7) /* Link status change */
#define ISR_LNKCHG (1 << 5) /* Link status change */
#define ISR_ROO (1 << 3)    /* Receive overflow counter overflow */
#define ISR_ROS (1 << 2)    /* Receive overflow */
#define ISR_PT (1 << 1)     /* Packet transmitted */
#define ISR_PR (1 << 0)     /* Packet received */
#define ISR_CLR_INT (ISR_LNKCHG | ISR_ROO | ISR_ROS | ISR_PT | ISR_PR)
#define ISR_STOP_MRCMD (ISR_IOMODE)
#define CH390_IMR 0x7F
#define IMR_NONE 0x00        /* Disable all interrupt */
#define IMR_ALL 0xFF         /* Enable all interrupt */
#define IMR_PAR (1 << 7)     /* Pointer auto-return mode */
#define IMR_LNKCHGI (1 << 5) /* Enable link status change interrupt */
#define IMR_UDRUNI (1 << 4)  /* Enable transmit under-run interrupt */
#define IMR_ROOI                                                              \
  (1 << 3) /* Enable receive overflow counter overflow interrupt */
#define IMR_ROI (1 << 2) /* Enable receive overflow interrupt */
#define IMR_PTI (1 << 1) /* Enable packet transmitted interrupt */
#define IMR_PRI (1 << 0) /* Enable packet received interrupt */

/* SPI commands */
#define OPC_REG_W 0x80     /* Register Write */
#define OPC_REG_R 0x00     /* Register Read */
#define OPC_MEM_DMY_R 0x70 /* Memory Dummy Read */
#define OPC_MEM_WRITE 0xF8 /* Memory Write */
#define OPC_MEM_READ 0x72  /* Memory Read */

/* GPIO */
#define CH390_GPIO1 0x02
#define CH390_GPIO2 0x04
#define CH390_GPIO3 0x08

/* PHY register */
#define CH390_PHY_ADDR 1
#define CH390_PHY 0x40
#define CH390_PHY_BMCR 0x00
#define CH390_PHY_BMSR 0x01
#define CH390_PHY_PHYID1 0x02
#define CH390_PHY_PHYID2 0x03
#define CH390_PHY_ANAR 0x04
#define CH390_PHY_ANLPAR 0x05
#define CH390_PHY_ANER 0x06
#define CH390_PHY_PAGE_SEL 0x1F
#define CH390_TX_QUE_HI_WATER 50
#define CH390_TX_QUE_LO_WATER 25
#define CH390_EEPROM_MAGIC 0x9151

/* Packet status */
#define CH390_PKT_NONE 0x00 /* No packet received */
#define CH390_PKT_RDY 0x01  /* Packet ready to receive */
#define CH390_PKT_ERR 0xFE  /* Un-stable states */
#define CH390_PKT_MAX 1536  /* Received packet max size */
#define CH390_PKT_MIN 64

typedef struct ch390_chip_t ch390_chip_t;

struct ch390_chip_t
{
  int (*reset)(ch390_chip_t *chip);
  int (*spi_select)(ch390_chip_t *chip, bool select);
  int (*spi_read)(ch390_chip_t *chip, uint8_t *data, unsigned int len);
  int (*spi_write)(ch390_chip_t *chip, const uint8_t *data, unsigned int len);
  void (*lock)(ch390_chip_t *chip);
  void (*unlock)(ch390_chip_t *chip);
  void (*sleep)(ch390_chip_t *chip, unsigned int ticks);
};

typedef struct
{
  uint8_t hash_bucket[8 * 8];
} ch390_mac_filter_t;

typedef struct
{
  ch390_chip_t *chip;
  ch390_mac_filter_t mac_filter;
  struct netif *netif;
} ch390_t;

int ch390_init(ch390_t *dev, struct netif *netif);
int ch390_receive(ch390_t *dev, struct pbuf **packet);
int ch390_transmit(ch390_t *dev, struct pbuf *packet);
int ch390_irq_handler(ch390_t *dev);

#endif // CH390_LINUX_CH390_H
