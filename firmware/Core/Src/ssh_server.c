#include "ssh_server.h"

#include <string.h>
#include "cmd.h"
#include "container_of.h"
#include "crypto.h"
#include "fifo.h"
#include "log.h"
#include "main.h"
#include "ssh_crypto.h"
#include "stream_notify.h"
#include "semphr.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"

#define SSH_IO_SIZE 1024U
#define SSH_WORKER_STACK 1536U
#define SSH_SHELL_STACK 1024U
#define SSH_HANDSHAKE_MS 30000U
#define SSH_STALL_MS 300000U
#define SSH_DRAIN_MS 1000U
#define SSH_SHELL_COUNT 2U
#define SSH_CONNECTION_COUNT (SSH_SHELL_COUNT + 1U)

typedef struct ssh_session ssh_session_t;

typedef struct
{
  ssh_session_t *session;
  TaskHandle_t task;
  StaticTask_t tcb;
  StackType_t stack[SSH_SHELL_STACK];
  StaticSemaphore_t start_buffer;
  SemaphoreHandle_t start;
} ssh_shell_t;

typedef enum
{
  SSH_FREE,
  SSH_HANDSHAKE,
  SSH_READY,
  SSH_RUNNING,
  SSH_DRAINING,
  SSH_STOPPING,
} ssh_state_t;

struct ssh_session
{
  stream_t stream;
  stream_notify_t notify;
  fifo_t rx;
  fifo_t tx;
  uint8_t rx_data[SSH_IO_SIZE];
  uint8_t tx_data[SSH_IO_SIZE];
  ssh_shell_t *shell;
  WOLFSSH *ssh;
  struct tcp_pcb *pcb;
  struct pbuf *tcp_rx;
  bool assigned;
  bool tcp_eof;
  bool write_blocked;
  ssh_state_t state;
  TickType_t activity;
  TickType_t admitted;
  bool closed;
  bool read_eof;
  bool shell_done;
  bool channel_open;
  bool request_accepted;
  int exit_code;
  char command[513];
};

static ssh_session_t ssh_sessions[SSH_CONNECTION_COUNT];
static ssh_shell_t ssh_shells[SSH_SHELL_COUNT];
static TaskHandle_t ssh_worker_task;
static StaticTask_t ssh_worker_tcb;
static StackType_t ssh_worker_stack[SSH_WORKER_STACK];
static ssh_session_t *ssh_admission;

static void
ssh_wake_worker()
{
  xTaskNotifyGive(ssh_worker_task);
}

static err_t
ssh_tcp_receive(void *argument,
                struct tcp_pcb *pcb,
                struct pbuf *input,
                err_t error)
{
  ssh_session_t *session = argument;
  if (input == NULL)
    {
      session->tcp_eof = true;
    }
  else
    {
      struct pbuf *copy = pbuf_clone(PBUF_RAW, PBUF_RAM, input);
      if (copy == NULL)
        {
          return ERR_MEM;
        }
      if (session->tcp_rx == NULL)
        {
          session->tcp_rx = copy;
        }
      else
        {
          pbuf_cat(session->tcp_rx, copy);
        }
      pbuf_free(input);
    }
  ssh_wake_worker();
  return ERR_OK;
}

static err_t
ssh_tcp_sent(void *argument, struct tcp_pcb *pcb, u16_t length)
{
  ssh_wake_worker();
  return ERR_OK;
}

static void
ssh_tcp_error(void *argument, err_t error)
{
  ssh_session_t *session = argument;
  session->pcb = NULL;
  ssh_wake_worker();
}

static err_t
ssh_tcp_retry(void *argument, struct tcp_pcb *pcb)
{
  ssh_session_t *session = argument;
  if (session->write_blocked)
    {
      session->write_blocked = tcp_output(pcb) != ERR_OK;
      ssh_wake_worker();
    }
  return ERR_OK;
}

static err_t
ssh_tcp_accept(void *argument, struct tcp_pcb *pcb, err_t error)
{
  if (error == ERR_OK && ssh_admission == NULL)
    {
      for (unsigned i = 0; i < SSH_CONNECTION_COUNT; ++i)
        {
          ssh_session_t *session = &ssh_sessions[i];
          if (!session->assigned)
            {
              session->assigned = true;
              session->pcb = pcb;
              ssh_admission = session;
              tcp_nagle_disable(pcb);
              tcp_arg(pcb, session);
              tcp_recv(pcb, ssh_tcp_receive);
              tcp_sent(pcb, ssh_tcp_sent);
              tcp_err(pcb, ssh_tcp_error);
              tcp_poll(pcb, ssh_tcp_retry, 1);
              ssh_wake_worker();
              return ERR_OK;
            }
        }
    }
  tcp_abort(pcb);
  return ERR_ABRT;
}

static void
ssh_tcp_close(ssh_session_t *session)
{
  LOCK_TCPIP_CORE();
  if (session->pcb != NULL)
    {
      struct tcp_pcb *pcb = session->pcb;
      tcp_arg(pcb, NULL);
      tcp_recv(pcb, NULL);
      tcp_sent(pcb, NULL);
      tcp_err(pcb, NULL);
      tcp_poll(pcb, NULL, 0);
      if (tcp_close(pcb) != ERR_OK)
        {
          tcp_abort(pcb);
        }
      session->pcb = NULL;
    }
  if (session->tcp_rx != NULL)
    {
      pbuf_free(session->tcp_rx);
      session->tcp_rx = NULL;
    }
  if (ssh_admission == session)
    {
      ssh_admission = NULL;
    }
  UNLOCK_TCPIP_CORE();
}

static void
ssh_notify_shell(ssh_session_t *session)
{
  stream_notify_send(&session->notify,
                     session->closed || session->read_eof
                         || fifo_size(&session->rx) != 0,
                     session->closed || fifo_size(&session->tx) < SSH_IO_SIZE);
}

static stream_status_t
ssh_exchange(stream_t *stream,
             uint8_t *read,
             uint8_t *write,
             stream_opts_t *opts)
{
  ssh_session_t *session = container_of(stream, ssh_session_t, stream);
  while (true)
    {
      taskENTER_CRITICAL();
      stream_status_t status = 0;
      if (session->closed)
        {
          status = STREAM_CLOSED;
        }
      else
        {
          bool rx_full = fifo_size(&session->rx) == SSH_IO_SIZE;
          bool tx_empty = fifo_size(&session->tx) == 0;
          if (read && fifo_getc(&session->rx, read))
            {
              status |= STREAM_READ_OK;
            }
          else if (read && session->read_eof)
            {
              status |= STREAM_END;
            }
          if (write && fifo_putc(&session->tx, *write))
            {
              status |= STREAM_WRITE_OK;
            }
          if ((rx_full && (status & STREAM_READ_OK))
              || (tx_empty && (status & STREAM_WRITE_OK)))
            {
              ssh_wake_worker();
            }
        }
      if (status != 0 || (!read && !write)
          || (opts != NULL && opts->nonblocking))
        {
          taskEXIT_CRITICAL();
          return status;
        }
      session->notify.waiter = xTaskGetCurrentTaskHandle();
      taskEXIT_CRITICAL();
      xTaskNotifyWait(0, UINT32_MAX, NULL, portMAX_DELAY);
      taskENTER_CRITICAL();
      session->notify.waiter = NULL;
      taskEXIT_CRITICAL();
    }
}

static void
ssh_notify(stream_t *stream, uint32_t read, uint32_t write)
{
  ssh_session_t *session = container_of(stream, ssh_session_t, stream);
  taskENTER_CRITICAL();
  stream_notify_set(&session->notify, read, write);
  ssh_notify_shell(session);
  taskEXIT_CRITICAL();
}

static void
ssh_shell_main(void *argument)
{
  ssh_shell_t *shell = argument;
  while (true)
    {
      xSemaphoreTake(shell->start, portMAX_DELAY);
      ssh_session_t *session = shell->session;
      int result = CMD_EXIT_OK;
      if (session->command[0] != '\0')
        {
          result = cmd_execute(&session->stream, session->command);
        }
      else
        {
          cmd_shell(&session->stream);
        }
      taskENTER_CRITICAL();
      session->exit_code = result;
      session->shell_done = true;
      taskEXIT_CRITICAL();
      ssh_wake_worker();
    }
}

static int
ssh_receive_io(WOLFSSH *ssh, void *buffer, word32 size, void *context)
{
  ssh_session_t *session = context;
  int result = WS_CBIO_ERR_WANT_READ;
  LOCK_TCPIP_CORE();
  if (session->pcb == NULL)
    {
      result = WS_CBIO_ERR_CONN_CLOSE;
    }
  else if (session->tcp_rx != NULL && size != 0)
    {
      u16_t length = (u16_t)LWIP_MIN(size, session->tcp_rx->tot_len);
      result = pbuf_copy_partial(session->tcp_rx, buffer, length, 0);
      session->tcp_rx = pbuf_free_header(session->tcp_rx, (u16_t)result);
      tcp_recved(session->pcb, (u16_t)result);
    }
  else if (session->tcp_eof)
    {
      result = WS_CBIO_ERR_CONN_CLOSE;
    }
  UNLOCK_TCPIP_CORE();
  if (result > 0)
    {
      ssh_wake_worker();
    }
  return result;
}

static int
ssh_send_io(WOLFSSH *ssh, void *buffer, word32 size, void *context)
{
  ssh_session_t *session = context;
  int result = WS_CBIO_ERR_WANT_WRITE;
  LOCK_TCPIP_CORE();
  if (session->pcb == NULL)
    {
      result = WS_CBIO_ERR_CONN_CLOSE;
    }
  else
    {
      u16_t length = (u16_t)LWIP_MIN(size, tcp_sndbuf(session->pcb));
      err_t error
          = length != 0
                ? tcp_write(session->pcb, buffer, length, TCP_WRITE_FLAG_COPY)
                : ERR_MEM;
      session->write_blocked = error == ERR_MEM;
      if (error == ERR_OK)
        {
          session->write_blocked = tcp_output(session->pcb) != ERR_OK;
          result = length;
        }
      else if (error != ERR_MEM)
        {
          result = WS_CBIO_ERR_GENERAL;
        }
    }
  UNLOCK_TCPIP_CORE();
  if (result > 0)
    {
      ssh_wake_worker();
    }
  return result;
}

static int
ssh_auth_types(WOLFSSH *ssh, void *context)
{
  return WOLFSSH_USERAUTH_PUBLICKEY;
}

static int
ssh_authenticate(byte type, WS_UserAuthData *auth, void *context)
{
  if (type != WOLFSSH_USERAUTH_PUBLICKEY)
    {
      return WOLFSSH_USERAUTH_INVALID_AUTHTYPE;
    }
  if (auth->usernameSz != 4 || memcmp(auth->username, "magi", 4) != 0)
    {
      return WOLFSSH_USERAUTH_INVALID_USER;
    }
  return ssh_authorize_key(auth->sf.publicKey.publicKey,
                           auth->sf.publicKey.publicKeySz)
             ? WOLFSSH_USERAUTH_SUCCESS
             : WOLFSSH_USERAUTH_INVALID_PUBLICKEY;
}

static int
ssh_channel_open(WOLFSSH_CHANNEL *channel, void *context)
{
  ssh_session_t *session = context;
  if (session->channel_open)
    {
      return WS_RESOURCE_E;
    }
  session->channel_open = true;
  return WS_SUCCESS;
}

static int
ssh_channel_request(WOLFSSH_CHANNEL *channel, void *context)
{
  ssh_session_t *session = context;
  if (session->request_accepted)
    {
      return WS_INVALID_STATE_E;
    }
  const char *command = wolfSSH_ChannelGetSessionCommand(channel);
  if (command != NULL)
    {
      size_t length = strlen(command);
      if (length == 0 || length >= sizeof(session->command))
        {
          return WS_BAD_ARGUMENT;
        }
      memcpy(session->command, command, length + 1);
    }
  session->request_accepted = true;
  return WS_SUCCESS;
}

static int
ssh_reject_subsystem(WOLFSSH_CHANNEL *channel, void *context)
{
  return WS_INVALID_CHANTYPE;
}

static bool
ssh_retryable(WOLFSSH *ssh, int result)
{
  int error = result == WS_FATAL_ERROR || result == WS_ERROR
                  ? wolfSSH_get_error(ssh)
                  : result;
  return error == WS_WANT_READ || error == WS_WANT_WRITE
         || error == WS_REKEYING || error == WS_WINDOW_FULL
         || error == WS_CHAN_RXD;
}

static int
ssh_accept(WOLFSSH *ssh)
{
  if (crypto_lock() != 0)
    {
      return WS_CRYPTO_FAILED;
    }
  int result = wolfSSH_accept(ssh);
  crypto_unlock();
  return result;
}

static int
ssh_stream_read(WOLFSSH *ssh, uint8_t *data, word32 size)
{
  if (crypto_lock() != 0)
    {
      return WS_CRYPTO_FAILED;
    }
  int result = wolfSSH_stream_read(ssh, data, size);
  crypto_unlock();
  return result;
}

static int
ssh_stream_send(WOLFSSH *ssh, uint8_t *data, word32 size)
{
  if (crypto_lock() != 0)
    {
      return WS_CRYPTO_FAILED;
    }
  int result = wolfSSH_stream_send(ssh, data, size);
  crypto_unlock();
  return result;
}

static int
ssh_worker(WOLFSSH *ssh)
{
  if (crypto_lock() != 0)
    {
      return WS_CRYPTO_FAILED;
    }
  int result = wolfSSH_worker(ssh, NULL);
  crypto_unlock();
  return result;
}

static int
ssh_stream_exit(WOLFSSH *ssh, int exit_code)
{
  if (crypto_lock() != 0)
    {
      return WS_CRYPTO_FAILED;
    }
  int result = wolfSSH_stream_exit(ssh, exit_code);
  crypto_unlock();
  return result;
}

static void
ssh_stop_session(ssh_session_t *session)
{
  if (session->state == SSH_FREE || session->state == SSH_STOPPING)
    {
      return;
    }
  session->state = SSH_STOPPING;
  taskENTER_CRITICAL();
  session->closed = true;
  ssh_notify_shell(session);
  taskEXIT_CRITICAL();
  if (session->shell != NULL)
    {
      xTaskNotify(session->shell->task, 0, eNoAction);
    }
  ssh_tcp_close(session);
  wolfSSH_free(session->ssh);
  session->ssh = NULL;
  ssh_wake_worker();
}

static void
ssh_start_session(WOLFSSH_CTX *ctx, ssh_session_t *session)
{
  session->stream
      = (stream_t){ .exchange = ssh_exchange, .notify = ssh_notify };
  fifo_init(&session->rx, session->rx_data, sizeof(session->rx_data));
  fifo_init(&session->tx, session->tx_data, sizeof(session->tx_data));
  session->state = SSH_HANDSHAKE;
  session->activity = xTaskGetTickCount();
  if (crypto_lock() == 0)
    {
      session->ssh = wolfSSH_new(ctx);
      crypto_unlock();
    }
  if (session->ssh == NULL)
    {
      ssh_stop_session(session);
      return;
    }
  wolfSSH_SetIOReadCtx(session->ssh, session);
  wolfSSH_SetIOWriteCtx(session->ssh, session);
  wolfSSH_SetChannelOpenCtx(session->ssh, session);
  wolfSSH_SetChannelReqCtx(session->ssh, session);
  ssh_wake_worker();
}

static void
ssh_admit_session(ssh_session_t *session)
{
  ssh_session_t *oldest = NULL;
  TickType_t now = xTaskGetTickCount();
  for (unsigned i = 0; i < SSH_SHELL_COUNT; ++i)
    {
      ssh_shell_t *shell = &ssh_shells[i];
      if (shell->session == NULL)
        {
          shell->session = session;
          session->shell = shell;
          session->state = SSH_RUNNING;
          session->admitted = session->activity = now;
          LOCK_TCPIP_CORE();
          ssh_admission = NULL;
          UNLOCK_TCPIP_CORE();
          xSemaphoreGive(shell->start);
          return;
        }
      if (shell->session->state == SSH_STOPPING)
        {
          return;
        }
      if (oldest == NULL
          || now - shell->session->admitted > now - oldest->admitted)
        {
          oldest = shell->session;
        }
    }
  ssh_stop_session(oldest);
}

static void
ssh_service_running(ssh_session_t *session)
{
  WOLFSSH *ssh = session->ssh;
  int result;
  uint8_t *tx = NULL;
  taskENTER_CRITICAL();
  uint32_t space = SSH_IO_SIZE - fifo_size(&session->rx);
  uint32_t length = fifo_dma_read(&session->tx, &tx, SSH_IO_SIZE);
  taskEXIT_CRITICAL();
  if (length != 0)
    {
      result = ssh_stream_send(ssh, tx, length);
      taskENTER_CRITICAL();
      fifo_dma_read_done(&session->tx, result > 0 ? (uint32_t)result : 0);
      if (result > 0)
        {
          ssh_notify_shell(session);
        }
      taskEXIT_CRITICAL();
      if (result > 0)
        {
          session->activity = xTaskGetTickCount();
          ssh_wake_worker();
        }
      else if (result < 0 && !ssh_retryable(ssh, result))
        {
          ssh_stop_session(session);
          return;
        }
    }
  taskENTER_CRITICAL();
  bool done = session->shell_done && fifo_size(&session->tx) == 0;
  taskEXIT_CRITICAL();
  if (done)
    {
      session->state = SSH_DRAINING;
      session->activity = xTaskGetTickCount();
      ssh_wake_worker();
      return;
    }
  int error = wolfSSH_get_error(ssh);
  if (error == WS_REKEYING || error == WS_WANT_WRITE || session->read_eof)
    {
      result = ssh_worker(ssh);
      if (result != WS_SUCCESS && result != WS_EOF
          && !ssh_retryable(ssh, result))
        {
          ssh_stop_session(session);
          return;
        }
    }
  if (space != 0 && !session->read_eof)
    {
      uint8_t rx[SSH_IO_SIZE];
      result = ssh_stream_read(ssh, rx, space);
      if (result > 0)
        {
          taskENTER_CRITICAL();
          fifo_write(&session->rx, rx, (uint32_t)result);
          ssh_notify_shell(session);
          taskEXIT_CRITICAL();
          session->activity = xTaskGetTickCount();
          ssh_wake_worker();
        }
      else if (result == WS_EOF || wolfSSH_get_error(ssh) == WS_EOF)
        {
          taskENTER_CRITICAL();
          session->read_eof = true;
          ssh_notify_shell(session);
          taskEXIT_CRITICAL();
          ssh_wake_worker();
        }
      else if (result < 0 && !ssh_retryable(ssh, result))
        {
          ssh_stop_session(session);
          return;
        }
    }
  if (xTaskGetTickCount() - session->activity >= pdMS_TO_TICKS(SSH_STALL_MS))
    {
      ssh_stop_session(session);
    }
}

static void
ssh_service_session(WOLFSSH_CTX *ctx, ssh_session_t *session)
{
  LOCK_TCPIP_CORE();
  bool assigned = session->assigned;
  bool disconnected = assigned && session->pcb == NULL;
  UNLOCK_TCPIP_CORE();
  if (disconnected)
    {
      if (session->state == SSH_FREE)
        {
          session->state = SSH_HANDSHAKE;
        }
      ssh_stop_session(session);
    }
  WOLFSSH *ssh = session->ssh;
  int result;
  switch (session->state)
    {
    case SSH_FREE:
      if (assigned)
        {
          ssh_start_session(ctx, session);
        }
      return;
    case SSH_STOPPING:
      {
        taskENTER_CRITICAL();
        bool done = session->shell == NULL || session->shell_done;
        taskEXIT_CRITICAL();
        if (done)
          {
            if (session->shell != NULL)
              {
                session->shell->session = NULL;
              }
            LOCK_TCPIP_CORE();
            memset(session, 0, sizeof(*session));
            UNLOCK_TCPIP_CORE();
            ssh_wake_worker();
          }
        return;
      }
    case SSH_HANDSHAKE:
      result = ssh_accept(ssh);
      if (result == WS_SUCCESS && session->request_accepted)
        {
          session->state = SSH_READY;
          ssh_admit_session(session);
        }
      else if (!ssh_retryable(ssh, result)
               || xTaskGetTickCount() - session->activity
                      >= pdMS_TO_TICKS(SSH_HANDSHAKE_MS))
        {
          ssh_stop_session(session);
        }
      return;
    case SSH_READY:
      ssh_admit_session(session);
      return;
    case SSH_DRAINING:
      result = ssh_stream_exit(ssh, session->exit_code);
      if (!ssh_retryable(ssh, result)
          || xTaskGetTickCount() - session->activity
                 >= pdMS_TO_TICKS(SSH_DRAIN_MS))
        {
          ssh_stop_session(session);
        }
      return;
    case SSH_RUNNING:
      ssh_service_running(session);
      return;
    }
}

static WOLFSSH_CTX *
ssh_create_context()
{
  if (crypto_lock() != 0)
    {
      return NULL;
    }
  int result = wolfSSH_Init();
  crypto_unlock();
  if (result != WS_SUCCESS)
    {
      Error_Handler();
    }
  WOLFSSH_CTX *ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, NULL);
  if (ctx == NULL)
    {
      Error_Handler();
    }
  wolfSSH_SetIORecv(ctx, ssh_receive_io);
  wolfSSH_SetIOSend(ctx, ssh_send_io);
  wolfSSH_SetUserAuth(ctx, ssh_authenticate);
  wolfSSH_SetUserAuthTypes(ctx, ssh_auth_types);
  wolfSSH_CTX_SetChannelOpenCb(ctx, ssh_channel_open);
  wolfSSH_CTX_SetChannelReqShellCb(ctx, ssh_channel_request);
  wolfSSH_CTX_SetChannelReqExecCb(ctx, ssh_channel_request);
  wolfSSH_CTX_SetChannelReqSubsysCb(ctx, ssh_reject_subsystem);
  if (wolfSSH_CTX_SetAlgoListKex(ctx, "ecdh-sha2-nistp256") != WS_SUCCESS
      || wolfSSH_CTX_SetAlgoListKey(ctx, "ecdsa-sha2-nistp256") != WS_SUCCESS
      || wolfSSH_CTX_SetAlgoListKeyAccepted(ctx, "ecdsa-sha2-nistp256")
             != WS_SUCCESS
      || wolfSSH_CTX_SetAlgoListCipher(ctx, "aes128-ctr") != WS_SUCCESS
      || wolfSSH_CTX_SetAlgoListMac(ctx, "hmac-sha2-256") != WS_SUCCESS)
    {
      Error_Handler();
    }
  bool reported = false;
  while (ssh_crypto_init(ctx) != 0)
    {
      if (!reported)
        {
          log_info("ssh", "waiting for provisioned ATECC608B host slot 1");
          reported = true;
        }
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  return ctx;
}

static bool
ssh_open_listener()
{
  LOCK_TCPIP_CORE();
  struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  struct tcp_pcb *listener = NULL;
  if (pcb != NULL)
    {
      if (tcp_bind(pcb, IP_ANY_TYPE, 22) == ERR_OK)
        {
          listener = tcp_listen_with_backlog(pcb, 1);
        }
      if (listener != NULL)
        {
          tcp_accept(listener, ssh_tcp_accept);
        }
      else
        {
          tcp_abort(pcb);
        }
    }
  UNLOCK_TCPIP_CORE();
  return listener != NULL;
}

static TickType_t
ssh_next_timeout()
{
  TickType_t wait = portMAX_DELAY;
  TickType_t now = xTaskGetTickCount();
  for (unsigned i = 0; i < SSH_CONNECTION_COUNT; ++i)
    {
      ssh_session_t *session = &ssh_sessions[i];
      TickType_t limit;
      switch (session->state)
        {
        case SSH_HANDSHAKE:
          limit = pdMS_TO_TICKS(SSH_HANDSHAKE_MS);
          break;
        case SSH_RUNNING:
          limit = pdMS_TO_TICKS(SSH_STALL_MS);
          break;
        case SSH_DRAINING:
          limit = pdMS_TO_TICKS(SSH_DRAIN_MS);
          break;
        default:
          continue;
        }
      TickType_t elapsed = now - session->activity;
      TickType_t remaining = elapsed < limit ? limit - elapsed : 0;
      if (remaining < wait)
        {
          wait = remaining;
        }
    }
  return wait;
}

static void
ssh_worker_main(void *argument)
{
  WOLFSSH_CTX *ctx;
  while ((ctx = ssh_create_context()) == NULL)
    {
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  while (!ssh_open_listener())
    {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  log_info("ssh", "listening on TCP port 22");
  while (true)
    {
      for (unsigned i = 0; i < SSH_CONNECTION_COUNT; ++i)
        {
          ssh_service_session(ctx, &ssh_sessions[i]);
        }
      ulTaskNotifyTake(pdTRUE, ssh_next_timeout());
    }
}

void
ssh_server_init()
{
  if (ssh_worker_task != NULL)
    {
      return;
    }
  for (unsigned i = 0; i < SSH_SHELL_COUNT; ++i)
    {
      char name[] = "ssh-shell-0";
      name[sizeof(name) - 2] = (char)('0' + i);
      ssh_shell_t *shell = &ssh_shells[i];
      shell->start = xSemaphoreCreateBinaryStatic(&shell->start_buffer);
      shell->task = xTaskCreateStatic(ssh_shell_main,
                                      name,
                                      SSH_SHELL_STACK,
                                      shell,
                                      tskIDLE_PRIORITY + 1,
                                      shell->stack,
                                      &shell->tcb);
    }
  ssh_worker_task = xTaskCreateStatic(ssh_worker_main,
                                      "ssh",
                                      SSH_WORKER_STACK,
                                      NULL,
                                      tskIDLE_PRIORITY + 1,
                                      ssh_worker_stack,
                                      &ssh_worker_tcb);
}
