#ifndef TCPM_H
#define TCPM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TCPM_PORT_COUNT 2U
#define TCPM_PORT_0 0U
#define TCPM_PORT_1 1U

#define PDO_MAX_OBJECTS 7U

#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif

enum pd_pdo_type
{
  PDO_TYPE_FIXED = 0,
  PDO_TYPE_BATT,
  PDO_TYPE_VAR,
  PDO_TYPE_APDO,
};

#define PDO_TYPE_SHIFT 30U
#define PDO_TYPE_MASK 0x3U
#define PDO_TYPE(t) ((uint32_t)(t) << PDO_TYPE_SHIFT)

#define PDO_VOLT_MASK 0x3ffU
#define PDO_CURR_MASK 0x3ffU
#define PDO_FIXED_DUAL_ROLE BIT(29)
#define PDO_FIXED_USB_COMM BIT(26)
#define PDO_FIXED_DATA_SWAP BIT(25)
#define PDO_FIXED_VOLT_SHIFT 10U
#define PDO_FIXED_CURR_SHIFT 0U
#define PDO_FIXED_VOLT(mv)                                                    \
  ((((mv) / 50U) & PDO_VOLT_MASK) << PDO_FIXED_VOLT_SHIFT)
#define PDO_FIXED_CURR(ma)                                                    \
  ((((ma) / 10U) & PDO_CURR_MASK) << PDO_FIXED_CURR_SHIFT)
#define PDO_FIXED(mv, ma, flags)                                              \
  (PDO_TYPE(PDO_TYPE_FIXED) | (flags) | PDO_FIXED_VOLT(mv)                    \
   | PDO_FIXED_CURR(ma))

#define RDO_OBJ_POS_SHIFT 28U
#define RDO_OBJ_POS_MASK 0x7U
#define RDO_CAP_MISMATCH BIT(26)
#define RDO_GIVE_BACK BIT(27)
#define RDO_USB_COMM BIT(25)
#define RDO_NO_SUSPEND BIT(24)
#define RDO_CURR_MASK 0x3ffU
#define RDO_FIXED_OP_CURR_SHIFT 10U
#define RDO_FIXED_MAX_CURR_SHIFT 0U
#define RDO_OBJ(idx) (((idx) & RDO_OBJ_POS_MASK) << RDO_OBJ_POS_SHIFT)
#define RDO_FIXED_OP_CURR(ma)                                                 \
  ((((ma) / 10U) & RDO_CURR_MASK) << RDO_FIXED_OP_CURR_SHIFT)
#define RDO_FIXED_MAX_CURR(ma)                                                \
  ((((ma) / 10U) & RDO_CURR_MASK) << RDO_FIXED_MAX_CURR_SHIFT)
#define RDO_FIXED(idx, op_ma, max_ma, flags)                                  \
  (RDO_OBJ(idx) | (flags) | RDO_FIXED_OP_CURR(op_ma)                          \
   | RDO_FIXED_MAX_CURR(max_ma))

#define TCPM_SGMII_SVID 0x8023U
#define TCPM_POWER_ORING_SVID 0x2020U

static inline enum pd_pdo_type
pdo_type(uint32_t pdo)
{
  return (enum pd_pdo_type)((pdo >> PDO_TYPE_SHIFT) & PDO_TYPE_MASK);
}

static inline uint32_t
pdo_fixed_voltage(uint32_t pdo)
{
  return ((pdo >> PDO_FIXED_VOLT_SHIFT) & PDO_VOLT_MASK) * 50U;
}

static inline uint32_t
pdo_fixed_current(uint32_t pdo)
{
  return ((pdo >> PDO_FIXED_CURR_SHIFT) & PDO_CURR_MASK) * 10U;
}

static inline uint32_t
rdo_index(uint32_t rdo)
{
  return (rdo >> RDO_OBJ_POS_SHIFT) & RDO_OBJ_POS_MASK;
}

static inline uint32_t
rdo_op_current(uint32_t rdo)
{
  return ((rdo >> RDO_FIXED_OP_CURR_SHIFT) & RDO_CURR_MASK) * 10U;
}

static inline uint32_t
rdo_max_current(uint32_t rdo)
{
  return ((rdo >> RDO_FIXED_MAX_CURR_SHIFT) & RDO_CURR_MASK) * 10U;
}

typedef enum
{
  TCPM_SINK = 0,
  TCPM_SOURCE,
} tcpm_typec_role_t;

typedef enum
{
  TCPM_DEVICE = 0,
  TCPM_HOST,
} tcpm_typec_data_role_t;

typedef enum
{
  TCPM_CC_NONE = 0,
  TCPM_CC_1,
  TCPM_CC_2,
} tcpm_cc_t;

typedef struct tcpm tcpm_t;

/* Negotiated mode state; cycle increments on each inactive-to-active
 * transition. */
typedef struct
{
  uint32_t cycles;
  bool active;
} tcpm_sgmii_status_t;

/* Internal API: callers provide valid ports and buffers. Peer data is
 * validated separately. */
extern tcpm_t *const tcpm;
void tcpm_init(tcpm_t *tcpm);

uint32_t tcpm_get_source_pdos(tcpm_t *tcpm, uint8_t port, uint32_t *pdos);
uint32_t tcpm_get_sink_pdos(tcpm_t *tcpm, uint8_t port, uint32_t *pdos);
void tcpm_set_partner_source_pdos(tcpm_t *tcpm,
                                  uint8_t port,
                                  const uint32_t *pdos,
                                  uint32_t count);
uint32_t tcpm_get_requested_pdo(tcpm_t *tcpm, uint8_t port);
void tcpm_clear_requested_pdo(tcpm_t *tcpm, uint8_t port);
bool tcpm_can_supply_requested_power(tcpm_t *tcpm, uint8_t port);

void tcpm_sync_port(tcpm_t *tcpm, uint8_t port);
void tcpm_set_attached(tcpm_t *tcpm, uint8_t port, bool attached);
void tcpm_contract_established(tcpm_t *tcpm, uint8_t port);
void tcpm_reset_contract(tcpm_t *tcpm, uint8_t port);

/* Source: validate a received RDO and select our corresponding source PDO. */
bool tcpm_validate_request(tcpm_t *tcpm, uint8_t port, uint32_t rdo);
/* Sink: select a partner source PDO and build the RDO to transmit. */
bool tcpm_build_request(tcpm_t *tcpm, uint8_t port, uint32_t *rdo);
bool tcpm_apply_requested_power(tcpm_t *tcpm, uint8_t port);
bool tcpm_set_source_power(tcpm_t *tcpm,
                           uint8_t port,
                           uint32_t voltage_mv,
                           uint32_t operating_current_ma,
                           uint32_t max_current_ma);
bool tcpm_is_source_power_enabled(tcpm_t *tcpm, uint8_t port);
bool tcpm_is_power_oring_active(tcpm_t *tcpm, uint8_t port);
bool tcpm_evaluate_power_role_swap(tcpm_t *tcpm, uint8_t port);

void tcpm_usb_start(tcpm_t *tcpm, uint8_t port);
void tcpm_usb_stop(tcpm_t *tcpm, uint8_t port);
tcpm_sgmii_status_t tcpm_get_sgmii_status(tcpm_t *tcpm, uint8_t port);

const uint16_t *tcpm_get_svids(tcpm_t *tcpm, uint8_t port, uint8_t *count);
const uint32_t *
tcpm_get_modes(tcpm_t *tcpm, uint8_t port, uint16_t svid, uint8_t *count);
bool tcpm_vdm_mode_enter(tcpm_t *tcpm,
                         uint8_t port,
                         uint16_t svid,
                         uint32_t mode_index);
bool tcpm_vdm_mode_exit(tcpm_t *tcpm,
                        uint8_t port,
                        uint16_t svid,
                        uint32_t mode_index);
void tcpm_vdm_identity_result(tcpm_t *tcpm,
                              uint8_t port,
                              bool accepted,
                              bool modal);
void tcpm_vdm_svids_result(tcpm_t *tcpm,
                           uint8_t port,
                           bool accepted,
                           const uint16_t *svids,
                           uint8_t count);
void tcpm_vdm_modes_result(tcpm_t *tcpm,
                           uint8_t port,
                           bool accepted,
                           uint16_t svid,
                           const uint32_t *modes,
                           uint8_t count);
void tcpm_vdm_enter_result(tcpm_t *tcpm,
                           uint8_t port,
                           bool accepted,
                           uint16_t svid,
                           uint32_t mode_index);
void tcpm_vdm_exit_result(tcpm_t *tcpm,
                          uint8_t port,
                          bool accepted,
                          uint16_t svid,
                          uint32_t mode_index);

#endif /* TCPM_H */
