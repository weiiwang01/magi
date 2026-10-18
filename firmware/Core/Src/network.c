#include "network.h"

#include <errno.h>
#include <ctype.h>
#include <string.h>

#include "board.h"
#include "ch390.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "timeout.h"
#include "log.h"
#include "ssh_server.h"
#include "lwip/apps/mdns.h"
#include "lwip/dhcp.h"
#include "lwip/tcpip.h"

#define NETWORK_SPI_TIMEOUT_MS 100U
#define NETWORK_TASK_STACK_SIZE (2048U / sizeof(StackType_t))
#define NETWORK_TASK_PRIORITY 32U

static SPI_HandleTypeDef network_spi;
static SemaphoreHandle_t network_mutex;
static SemaphoreHandle_t network_config_mutex;
static TaskHandle_t network_task_handle;
static struct netif network_netif;
static char network_hostname[NETWORK_HOSTNAME_MAX + 1];
static bool network_ready;

static void
network_config_lock()
{
  if (network_config_mutex != NULL
      && xSemaphoreTake(network_config_mutex, portMAX_DELAY) != pdTRUE)
    {
      Error_Handler();
    }
}

static void
network_config_unlock()
{
  if (network_config_mutex != NULL
      && xSemaphoreGive(network_config_mutex) != pdTRUE)
    {
      Error_Handler();
    }
}

void
network_get_hostname(char hostname[static NETWORK_HOSTNAME_MAX + 1])
{
  network_config_lock();
  memcpy(hostname, network_hostname, sizeof(network_hostname));
  if (hostname[0] == '\0')
    {
      network_default_hostname(hostname);
    }
  network_config_unlock();
}

int
network_set_hostname(const char *hostname)
{
  if (hostname == NULL)
    {
      return EINVAL;
    }
  size_t length = 0;
  while (hostname[length] != '\0')
    {
      char c = hostname[length];
      if (length == NETWORK_HOSTNAME_MAX
          || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
               || (c >= '0' && c <= '9') || c == '-'))
        {
          return EINVAL;
        }
      length += 1;
    }
  if (length == 0 || hostname[0] == '-' || hostname[length - 1] == '-')
    {
      return EINVAL;
    }

  int error = 0;
  network_config_lock();
  if (strcmp(network_hostname, hostname) != 0)
    {
      if (network_ready)
        {
          LOCK_TCPIP_CORE();
          if (mdns_resp_rename_netif(&network_netif, hostname) != ERR_OK)
            {
              error = EIO;
            }
          else
            {
              memcpy(network_hostname, hostname, length + 1);
            }
          UNLOCK_TCPIP_CORE();
        }
      else
        {
          memcpy(network_hostname, hostname, length + 1);
        }
    }
  network_config_unlock();
  return error;
}

static const ip6_addr_t *
network_ipv6_address(bool link_local)
{
  const ip6_addr_t *fallback = NULL;
  for (int i = 0; i < LWIP_IPV6_NUM_ADDRESSES; ++i)
    {
      const ip6_addr_t *address = netif_ip6_addr(&network_netif, i);
      uint8_t state = netif_ip6_addr_state(&network_netif, i);
      if (ip6_addr_isany(address)
          || !!ip6_addr_islinklocal(address) != link_local)
        {
          continue;
        }
      if (ip6_addr_ispreferred(state))
        {
          return address;
        }
      if (fallback == NULL && ip6_addr_isdeprecated(state))
        {
          fallback = address;
        }
    }
  return fallback;
}

bool
network_get_ipv4_address(char value[static NETWORK_ADDRESS_SIZE])
{
  bool found = false;
  value[0] = '\0';
  network_config_lock();
  if (network_ready)
    {
      LOCK_TCPIP_CORE();
      const ip4_addr_t *ip = netif_ip4_addr(&network_netif);
      const ip4_addr_t *netmask = netif_ip4_netmask(&network_netif);
      if (!ip4_addr_isany_val(*ip) && ip4_addr_netmask_valid(netmask->addr)
          && ip4addr_ntoa_r(ip, value, NETWORK_ADDRESS_SIZE - 4) != NULL)
        {
          unsigned int prefix = 0;
          for (uint32_t mask = lwip_ntohl(netmask->addr); mask != 0;
               mask <<= 1)
            {
              prefix += 1;
            }
          size_t length = strlen(value);
          value[length] = '/';
          length += 1;
          lwip_itoa(
              value + length, NETWORK_ADDRESS_SIZE - length, (int)prefix);
          found = true;
        }
      UNLOCK_TCPIP_CORE();
    }
  network_config_unlock();
  return found;
}

static bool
network_get_ipv6_cidr(bool link_local, char value[static NETWORK_ADDRESS_SIZE])
{
  bool found = false;
  value[0] = '\0';
  network_config_lock();
  if (network_ready)
    {
      LOCK_TCPIP_CORE();
      const ip6_addr_t *ip = network_ipv6_address(link_local);
      if (ip != NULL
          && ip6addr_ntoa_r(ip, value, NETWORK_ADDRESS_SIZE - 4) != NULL)
        {
          char *end = value;
          while (*end != '\0')
            {
              *end = (char)tolower((unsigned char)*end);
              end += 1;
            }
          strcpy(end, "/64");
          found = true;
        }
      UNLOCK_TCPIP_CORE();
    }
  network_config_unlock();
  return found;
}

bool
network_get_ipv6_address(char value[static NETWORK_ADDRESS_SIZE])
{
  return network_get_ipv6_cidr(false, value);
}

bool
network_get_ipv6_link_local_address(char value[static NETWORK_ADDRESS_SIZE])
{
  return network_get_ipv6_cidr(true, value);
}

static void
network_format_hostname(char hostname[static NETWORK_HOSTNAME_SIZE],
                        const uint32_t uid[static 3])
{
  static const char alphabet[] = "0123456789abcdefghjkmnpqrstvwxyz";
  uint32_t words[3] = { uid[0], uid[1], uid[2] };

  memcpy(hostname, "magi-", 5);
  for (unsigned int i = NETWORK_HOSTNAME_SIZE - 2U; i >= 5U; --i)
    {
      hostname[i] = alphabet[words[0] & 31U];
      words[0] = (words[0] >> 5) | (words[1] << 27);
      words[1] = (words[1] >> 5) | (words[2] << 27);
      words[2] >>= 5;
    }
  hostname[NETWORK_HOSTNAME_SIZE - 1U] = '\0';
}

void
network_default_hostname(char hostname[static NETWORK_HOSTNAME_SIZE])
{
  const uint32_t uid[] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };
  network_format_hostname(hostname, uid);
}

static void
network_sleep(ch390_chip_t *chip, unsigned int milliseconds)
{
  timeout_sleep_ms(milliseconds);
}

static int
network_reset(ch390_chip_t *chip)
{
  HAL_GPIO_WritePin(
      BOARD_CH390_RESET_GPIO_PORT, BOARD_CH390_RESET_GPIO_PIN, GPIO_PIN_RESET);
  timeout_sleep_ms(10);
  HAL_GPIO_WritePin(
      BOARD_CH390_RESET_GPIO_PORT, BOARD_CH390_RESET_GPIO_PIN, GPIO_PIN_SET);
  timeout_sleep_ms(10);
  return 0;
}

static int
network_spi_select(ch390_chip_t *chip, bool select)
{
  HAL_GPIO_WritePin(BOARD_CH390_CS_GPIO_PORT,
                    BOARD_CH390_CS_GPIO_PIN,
                    select ? GPIO_PIN_RESET : GPIO_PIN_SET);
  return 0;
}

static int
network_spi_read(ch390_chip_t *chip, uint8_t *data, unsigned int len)
{
  if (len == 0U || len > UINT16_MAX)
    {
      return EINVAL;
    }
  memset(data, 0, len);
  return HAL_SPI_Receive(
             &network_spi, data, (uint16_t)len, NETWORK_SPI_TIMEOUT_MS)
                 == HAL_OK
             ? 0
             : EIO;
}

static int
network_spi_write(ch390_chip_t *chip, const uint8_t *data, unsigned int len)
{
  if (len == 0U || len > UINT16_MAX)
    {
      return EINVAL;
    }
  return HAL_SPI_Transmit(
             &network_spi, data, (uint16_t)len, NETWORK_SPI_TIMEOUT_MS)
                 == HAL_OK
             ? 0
             : EIO;
}

static void
network_lock(ch390_chip_t *chip)
{
  if (xSemaphoreTake(network_mutex, portMAX_DELAY) != pdTRUE)
    {
      Error_Handler();
    }
}

static void
network_unlock(ch390_chip_t *chip)
{
  if (xSemaphoreGive(network_mutex) != pdTRUE)
    {
      Error_Handler();
    }
}

static ch390_chip_t network_chip = {
  .reset = network_reset,
  .spi_select = network_spi_select,
  .spi_read = network_spi_read,
  .spi_write = network_spi_write,
  .lock = network_lock,
  .unlock = network_unlock,
  .sleep = network_sleep,
};
static ch390_t network_device = { .chip = &network_chip };

static void
network_hardware_init()
{
  GPIO_InitTypeDef gpio = { 0 };

  BOARD_CH390_SPI_CLK_ENABLE();
  BOARD_CH390_SPI_SCK_GPIO_CLK_ENABLE();
  BOARD_CH390_SPI_MISO_GPIO_CLK_ENABLE();
  BOARD_CH390_SPI_MOSI_GPIO_CLK_ENABLE();
  BOARD_CH390_CS_GPIO_CLK_ENABLE();
  BOARD_CH390_RESET_GPIO_CLK_ENABLE();
  BOARD_CH390_INTERRUPT_GPIO_CLK_ENABLE();

  HAL_GPIO_WritePin(
      BOARD_CH390_CS_GPIO_PORT, BOARD_CH390_CS_GPIO_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(
      BOARD_CH390_RESET_GPIO_PORT, BOARD_CH390_RESET_GPIO_PIN, GPIO_PIN_RESET);
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Pin = BOARD_CH390_CS_GPIO_PIN;
  HAL_GPIO_Init(BOARD_CH390_CS_GPIO_PORT, &gpio);
  gpio.Pin = BOARD_CH390_RESET_GPIO_PIN;
  HAL_GPIO_Init(BOARD_CH390_RESET_GPIO_PORT, &gpio);

  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pin = BOARD_CH390_SPI_SCK_GPIO_PIN;
  gpio.Alternate = BOARD_CH390_SPI_SCK_GPIO_AF;
  HAL_GPIO_Init(BOARD_CH390_SPI_SCK_GPIO_PORT, &gpio);
  gpio.Pin = BOARD_CH390_SPI_MISO_GPIO_PIN;
  gpio.Alternate = BOARD_CH390_SPI_MISO_GPIO_AF;
  HAL_GPIO_Init(BOARD_CH390_SPI_MISO_GPIO_PORT, &gpio);
  gpio.Pin = BOARD_CH390_SPI_MOSI_GPIO_PIN;
  gpio.Alternate = BOARD_CH390_SPI_MOSI_GPIO_AF;
  HAL_GPIO_Init(BOARD_CH390_SPI_MOSI_GPIO_PORT, &gpio);

  gpio.Mode = GPIO_MODE_IT_RISING;
  gpio.Pull = GPIO_PULLDOWN;
  gpio.Pin = BOARD_CH390_INTERRUPT_GPIO_PIN;
  HAL_GPIO_Init(BOARD_CH390_INTERRUPT_GPIO_PORT, &gpio);
  HAL_NVIC_SetPriority(BOARD_CH390_INTERRUPT_IRQn, 3, 0);

  network_spi.Instance = BOARD_CH390_SPI_INSTANCE;
  network_spi.Init.Mode = SPI_MODE_MASTER;
  network_spi.Init.Direction = SPI_DIRECTION_2LINES;
  network_spi.Init.DataSize = SPI_DATASIZE_8BIT;
  network_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
  network_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
  network_spi.Init.NSS = SPI_NSS_SOFT;
  network_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  network_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
  network_spi.Init.TIMode = SPI_TIMODE_DISABLE;
  network_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  network_spi.Init.CRCPolynomial = 7;
  network_spi.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  network_spi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&network_spi) != HAL_OK)
    {
      Error_Handler();
    }
}

static err_t
network_netif_init(struct netif *netif)
{
  if (ch390_init(&network_device, netif) != 0)
    {
      return ERR_IF;
    }

  static const uint8_t zero_mac[6] = { 0 };
  if ((netif->hwaddr[0] & 1U) != 0U
      || memcmp(netif->hwaddr, zero_mac, sizeof(zero_mac)) == 0)
    {
      return ERR_IF;
    }
  netif_set_hostname(netif, network_hostname);
  return ERR_OK;
}

static void
network_tcpip_ready(void *argument)
{
  xTaskNotifyGive(network_task_handle);
}

static void
network_task(void *argument)
{
  struct netif *netif;
  network_hardware_init();
  tcpip_init(network_tcpip_ready, NULL);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  for (;;)
    {
      network_config_lock();
      LOCK_TCPIP_CORE();
      netif = netif_add(&network_netif,
                        IP4_ADDR_ANY4,
                        IP4_ADDR_ANY4,
                        IP4_ADDR_ANY4,
                        &network_device,
                        network_netif_init,
                        tcpip_input);
      UNLOCK_TCPIP_CORE();
      if (netif != NULL)
        {
          break;
        }
      network_config_unlock();
      log_info("network", "CH390 initialization failed, retrying");
      timeout_sleep_ms(1000);
    }

  LOCK_TCPIP_CORE();
  netif_set_default(netif);
  netif_set_ip6_autoconfig_enabled(netif, 1);
  netif_create_ip6_linklocal_address(netif, 1);
  netif_set_up(netif);
  mdns_resp_init();
  if (mdns_resp_add_netif(netif, network_hostname) != ERR_OK
      || dhcp_start(netif) != ERR_OK)
    {
      Error_Handler();
    }
  network_ready = true;
  UNLOCK_TCPIP_CORE();
  network_config_unlock();
  char hostname[NETWORK_HOSTNAME_MAX + 1];
  network_get_hostname(hostname);
  log_info("network", "use hostname: %s.local", hostname);

  __HAL_GPIO_EXTI_CLEAR_RISING_IT(BOARD_CH390_INTERRUPT_GPIO_PIN);
  HAL_NVIC_EnableIRQ(BOARD_CH390_INTERRUPT_IRQn);

  ssh_server_init();

  for (;;)
    {
      int error = ch390_irq_handler(&network_device);
      if (error != 0)
        {
          timeout_sleep_ms(10);
          continue;
        }
      if (HAL_GPIO_ReadPin(BOARD_CH390_INTERRUPT_GPIO_PORT,
                           BOARD_CH390_INTERRUPT_GPIO_PIN)
          == GPIO_PIN_SET)
        {
          continue;
        }
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

void
network_init()
{
  if (network_task_handle != NULL)
    {
      return;
    }
  if (network_hostname[0] == '\0')
    {
      network_default_hostname(network_hostname);
    }

  network_mutex = xSemaphoreCreateMutex();
  network_config_mutex = xSemaphoreCreateMutex();
  if (network_mutex == NULL || network_config_mutex == NULL)
    {
      Error_Handler();
    }
  if (xTaskCreate(network_task,
                  "ch390-rx",
                  NETWORK_TASK_STACK_SIZE,
                  NULL,
                  NETWORK_TASK_PRIORITY,
                  &network_task_handle)
      != pdPASS)
    {
      Error_Handler();
    }
}

void
network_exti_irq()
{
  if (__HAL_GPIO_EXTI_GET_RISING_IT(BOARD_CH390_INTERRUPT_GPIO_PIN) != 0U)
    {
      __HAL_GPIO_EXTI_CLEAR_RISING_IT(BOARD_CH390_INTERRUPT_GPIO_PIN);
      BaseType_t task_woken = pdFALSE;
      vTaskNotifyGiveFromISR(network_task_handle, &task_woken);
      portYIELD_FROM_ISR(task_woken);
    }
}
