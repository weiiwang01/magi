#include "tcpm.h"

#include <string.h>

#include "cmsis_gcc.h"
#include "usb.h"
#include "usbpd_def.h"
#include "usbpd_dpm_core.h"
#include "usbpd_dpm_user.h"

#define TCPM_5V_MV 5000U
#define TCPM_20V_MV 20000U
#define TCPM_3A_MA 3000U
#define TCPM_5A_MA 5000U
#define TCPM_PDO_FLAGS                                                        \
  (PDO_FIXED_DUAL_ROLE | PDO_FIXED_USB_COMM | PDO_FIXED_DATA_SWAP)

typedef enum
{
  TCPM_VDM_IDLE,
  TCPM_VDM_DISCOVER_IDENTITY,
  TCPM_VDM_DISCOVER_SVIDS,
  TCPM_VDM_DISCOVER_POWER_MODES,
  TCPM_VDM_ENTER_POWER_MODE,
  TCPM_VDM_DISCOVER_SGMII_MODES,
  TCPM_VDM_ENTER_SGMII_MODE,
  TCPM_VDM_READY,
} tcpm_vdm_state_t;

typedef struct
{
  uint32_t partner_source_pdos[PDO_MAX_OBJECTS];
  uint32_t partner_source_count;
  uint32_t requested_pdo;
  uint32_t requested_rdo;
  tcpm_typec_role_t power_role;
  tcpm_typec_data_role_t data_role;
  tcpm_cc_t cc;
  tcpm_vdm_state_t vdm_state;
  bool attached;
  bool explicit_contract;
  bool source_power_enabled;
  bool partner_has_sgmii;
  bool power_oring_active;
  tcpm_sgmii_status_t sgmii;
} tcpm_port_t;

struct tcpm
{
  tcpm_port_t ports[TCPM_PORT_COUNT];
};

static tcpm_t tcpm_instance;
tcpm_t *const tcpm = &tcpm_instance;

static const uint32_t source_pdos[] = {
  PDO_FIXED(TCPM_5V_MV, TCPM_3A_MA, TCPM_PDO_FLAGS),
};
static const uint32_t sink_pdos[TCPM_PORT_COUNT][2] = {
  { PDO_FIXED(TCPM_5V_MV, TCPM_3A_MA, TCPM_PDO_FLAGS),
    PDO_FIXED(TCPM_20V_MV, TCPM_3A_MA, 0U) },
  { PDO_FIXED(TCPM_5V_MV, TCPM_3A_MA, TCPM_PDO_FLAGS),
    PDO_FIXED(TCPM_20V_MV, TCPM_5A_MA, 0U) },
};
static const uint16_t svids[] = { TCPM_POWER_ORING_SVID, TCPM_SGMII_SVID };
static const uint32_t mode_vdo = 0x6d616769;

static void tcpm_vdm_start(tcpm_t *tcpm, uint8_t port);

static uint32_t
tcpm_lock()
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void
tcpm_unlock(uint32_t primask)
{
  if (!primask)
    {
      __enable_irq();
    }
}

static int
tcpm_update_usb_data_locked(tcpm_port_t *p, uint8_t port)
{
  if (!p->attached || p->cc == TCPM_CC_NONE
      || (!p->sgmii.active && p->data_role != TCPM_HOST))
    {
      return usb_set_data(
          (usb_t)port, USB_DATA_NONE, USB_DATA_NONE, USB_DATA_NONE);
    }
  usb_data_t other_lane = p->sgmii.active ? USB_DATA_ALT_MODE : USB_DATA_NONE;
  return usb_set_data((usb_t)port,
                      USB_DATA_USB,
                      p->cc == TCPM_CC_1 ? USB_DATA_USB : other_lane,
                      p->cc == TCPM_CC_2 ? USB_DATA_USB : other_lane);
}

static void
tcpm_stop_sgmii_locked(tcpm_port_t *p)
{
  p->sgmii.active = false;
}

static void
tcpm_stop_power_oring_locked(tcpm_port_t *p, uint8_t port)
{
  if (p->power_oring_active)
    {
      bool keep_source = p->attached && p->explicit_contract
                         && p->power_role == TCPM_SOURCE
                         && p->source_power_enabled;
      p->power_oring_active = false;
      usb_set_power((usb_t)port, keep_source ? USB_POWER_5V : USB_POWER_0V);
    }
}

static void
tcpm_reset_modes_locked(tcpm_port_t *p, uint8_t port)
{
  tcpm_stop_sgmii_locked(p);
  tcpm_stop_power_oring_locked(p, port);
  p->vdm_state = TCPM_VDM_IDLE;
  p->partner_has_sgmii = false;
}

static bool
tcpm_set_source_power_locked(tcpm_port_t *p, uint8_t port, bool enabled)
{
  if (!p->power_oring_active
      && usb_set_power((usb_t)port, enabled ? USB_POWER_5V : USB_POWER_0V)
             != USB_SUCCESS)
    {
      return false;
    }
  p->source_power_enabled = enabled;
  return true;
}

static bool
tcpm_can_supply_requested_power_locked(const tcpm_port_t *p)
{
  return p->power_role == TCPM_SOURCE && p->requested_pdo != 0U;
}

void
tcpm_init(tcpm_t *tcpm)
{
  memset(tcpm, 0, sizeof(*tcpm));
}

uint32_t
tcpm_get_source_pdos(tcpm_t *tcpm, uint8_t port, uint32_t *pdos)
{
  memcpy(pdos, source_pdos, sizeof(source_pdos));
  return sizeof(source_pdos) / sizeof(source_pdos[0]);
}

uint32_t
tcpm_get_sink_pdos(tcpm_t *tcpm, uint8_t port, uint32_t *pdos)
{
  memcpy(pdos, sink_pdos[port], sizeof(sink_pdos[port]));
  return sizeof(sink_pdos[port]) / sizeof(sink_pdos[port][0]);
}

void
tcpm_set_partner_source_pdos(tcpm_t *tcpm,
                             uint8_t port,
                             const uint32_t *pdos,
                             uint32_t count)
{
  tcpm_port_t *p = &tcpm->ports[port];
  uint32_t primask = tcpm_lock();
  memcpy(p->partner_source_pdos, pdos, count * sizeof(*pdos));
  p->partner_source_count = count;
  tcpm_unlock(primask);
}

uint32_t
tcpm_get_requested_pdo(tcpm_t *tcpm, uint8_t port)
{
  uint32_t primask = tcpm_lock();
  uint32_t pdo = tcpm->ports[port].requested_pdo;
  tcpm_unlock(primask);
  return pdo;
}

void
tcpm_clear_requested_pdo(tcpm_t *tcpm, uint8_t port)
{
  uint32_t primask = tcpm_lock();
  tcpm->ports[port].requested_pdo = 0U;
  tcpm->ports[port].requested_rdo = 0U;
  tcpm_unlock(primask);
}

bool
tcpm_can_supply_requested_power(tcpm_t *tcpm, uint8_t port)
{
  uint32_t primask = tcpm_lock();
  bool ready = tcpm_can_supply_requested_power_locked(&tcpm->ports[port]);
  tcpm_unlock(primask);
  return ready;
}

void
tcpm_sync_port(tcpm_t *tcpm, uint8_t port)
{
  tcpm_port_t *p = &tcpm->ports[port];
  uint32_t primask = tcpm_lock();
  tcpm_typec_role_t power_role
      = DPM_Params[port].PE_PowerRole == USBPD_PORTPOWERROLE_SRC ? TCPM_SOURCE
                                                                 : TCPM_SINK;
  tcpm_typec_data_role_t data_role
      = DPM_Params[port].PE_DataRole == USBPD_PORTDATAROLE_DFP ? TCPM_HOST
                                                               : TCPM_DEVICE;
  tcpm_cc_t cc = TCPM_CC_NONE;
  if (DPM_Params[port].ActiveCCIs == CC1)
    {
      cc = TCPM_CC_1;
    }
  else if (DPM_Params[port].ActiveCCIs == CC2)
    {
      cc = TCPM_CC_2;
    }

  if (p->power_role != power_role)
    {
      p->requested_pdo = 0U;
      p->requested_rdo = 0U;
      p->explicit_contract = false;
    }
  bool data_changed = p->data_role != data_role;
  p->power_role = power_role;
  p->data_role = data_role;
  p->cc = cc;
  if (data_changed)
    {
      tcpm_reset_modes_locked(p, port);
      tcpm_update_usb_data_locked(p, port);
    }
  tcpm_unlock(primask);
}

void
tcpm_set_attached(tcpm_t *tcpm, uint8_t port, bool attached)
{
  tcpm_port_t *p = &tcpm->ports[port];
  uint32_t primask = tcpm_lock();
  p->attached = attached;
  if (attached)
    {
      usb_set_vbus_discharge((usb_t)port, false);
      if (p->power_role == TCPM_SOURCE)
        {
          tcpm_set_source_power_locked(p, port, true);
        }
    }
  else
    {
      p->explicit_contract = false;
      p->requested_pdo = 0U;
      p->requested_rdo = 0U;
      p->partner_source_count = 0U;
      tcpm_reset_modes_locked(p, port);
      tcpm_set_source_power_locked(p, port, false);
      usb_set_vbus_discharge((usb_t)port, true);
    }
  tcpm_update_usb_data_locked(p, port);
  tcpm_unlock(primask);
}

void
tcpm_contract_established(tcpm_t *tcpm, uint8_t port)
{
  uint32_t primask = tcpm_lock();
  tcpm->ports[port].explicit_contract = true;
  tcpm_unlock(primask);
  tcpm_vdm_start(tcpm, port);
}

void
tcpm_reset_contract(tcpm_t *tcpm, uint8_t port)
{
  tcpm_port_t *p = &tcpm->ports[port];
  uint32_t primask = tcpm_lock();
  p->explicit_contract = false;
  p->requested_pdo = 0U;
  p->requested_rdo = 0U;
  tcpm_reset_modes_locked(p, port);
  tcpm_update_usb_data_locked(p, port);
  tcpm_unlock(primask);
}

void
tcpm_get_power_contract(tcpm_t *tcpm,
                        uint8_t port,
                        uint32_t *voltage_mv,
                        uint32_t *current_ma)
{
  uint32_t primask = tcpm_lock();
  tcpm_port_t *p = &tcpm->ports[port];
  uint32_t pdo = p->explicit_contract ? p->requested_pdo : 0U;
  uint32_t rdo = p->explicit_contract ? p->requested_rdo : 0U;
  tcpm_unlock(primask);
  *voltage_mv = pdo_fixed_voltage(pdo);
  *current_ma = rdo_op_current(rdo);
}

bool
tcpm_validate_request(tcpm_t *tcpm, uint8_t port, uint32_t rdo)
{
  tcpm_port_t *p = &tcpm->ports[port];
  uint32_t operating = rdo_op_current(rdo);
  uint32_t maximum = rdo_max_current(rdo);
  bool valid = rdo_index(rdo) == 1U && !(rdo & RDO_GIVE_BACK) && operating > 0U
               && operating <= TCPM_3A_MA && maximum >= operating
               && (maximum <= TCPM_3A_MA || (rdo & RDO_CAP_MISMATCH));
  uint32_t primask = tcpm_lock();
  if (p->power_role != TCPM_SOURCE)
    {
      valid = false;
    }
  else
    {
      p->requested_pdo = valid ? source_pdos[0] : 0U;
      p->requested_rdo = valid ? rdo : 0U;
    }
  tcpm_unlock(primask);
  return valid;
}

bool
tcpm_build_request(tcpm_t *tcpm, uint8_t port, uint32_t *rdo)
{
  tcpm_port_t *p = &tcpm->ports[port];
  uint32_t best_voltage = 0U;
  uint32_t desired_current = 0U;
  uint32_t current = 0U;
  uint32_t position = 0U;
  uint32_t primask = tcpm_lock();

  *rdo = 0U;
  if (p->power_role != TCPM_SINK)
    {
      tcpm_unlock(primask);
      return false;
    }
  p->requested_pdo = 0U;
  p->requested_rdo = 0U;
  for (uint32_t i = 0U; i < p->partner_source_count; ++i)
    {
      uint32_t source = p->partner_source_pdos[i];
      if (pdo_type(source) != PDO_TYPE_FIXED)
        {
          continue;
        }
      for (uint32_t j = 0U;
           j < sizeof(sink_pdos[port]) / sizeof(sink_pdos[port][0]);
           ++j)
        {
          uint32_t sink = sink_pdos[port][j];
          if (pdo_fixed_voltage(source) != pdo_fixed_voltage(sink))
            {
              continue;
            }
          uint32_t available = pdo_fixed_current(source);
          uint32_t desired = pdo_fixed_current(sink);
          uint32_t ma = available < desired ? available : desired;
          uint32_t voltage = pdo_fixed_voltage(source);
          if (ma > 0U && voltage > best_voltage)
            {
              best_voltage = voltage;
              current = ma;
              desired_current = desired;
              position = i + 1U;
              p->requested_pdo = source;
            }
        }
    }
  if (position != 0U)
    {
      uint32_t flags = RDO_USB_COMM | RDO_NO_SUSPEND;
      if (current < desired_current)
        {
          flags |= RDO_CAP_MISMATCH;
        }
      *rdo = RDO_FIXED(position, current, desired_current, flags);
      p->requested_rdo = *rdo;
    }
  tcpm_unlock(primask);
  return position != 0U;
}

bool
tcpm_apply_requested_power(tcpm_t *tcpm, uint8_t port)
{
  tcpm_port_t *p = &tcpm->ports[port];
  uint32_t primask = tcpm_lock();
  bool applied = tcpm_can_supply_requested_power_locked(p)
                 && tcpm_set_source_power_locked(p, port, true);
  tcpm_unlock(primask);
  return applied;
}

bool
tcpm_set_source_power(tcpm_t *tcpm,
                      uint8_t port,
                      uint32_t voltage_mv,
                      uint32_t operating_current_ma,
                      uint32_t max_current_ma)
{
  if (voltage_mv != 0U
      && (voltage_mv != TCPM_5V_MV || operating_current_ma > TCPM_3A_MA
          || max_current_ma > TCPM_3A_MA))
    {
      return false;
    }
  uint32_t primask = tcpm_lock();
  bool applied = tcpm_set_source_power_locked(
      &tcpm->ports[port], port, voltage_mv != 0U);
  tcpm_unlock(primask);
  return applied;
}

bool
tcpm_is_source_power_enabled(tcpm_t *tcpm, uint8_t port)
{
  uint32_t primask = tcpm_lock();
  bool enabled = tcpm->ports[port].source_power_enabled;
  tcpm_unlock(primask);
  return enabled;
}

bool
tcpm_is_power_oring_active(tcpm_t *tcpm, uint8_t port)
{
  uint32_t primask = tcpm_lock();
  bool active = tcpm->ports[port].power_oring_active;
  tcpm_unlock(primask);
  return active;
}

bool
tcpm_evaluate_power_role_swap(tcpm_t *tcpm, uint8_t port)
{
  uint32_t primask = tcpm_lock();
  bool accept = tcpm->ports[port].power_role == TCPM_SOURCE;
  tcpm_unlock(primask);
  return accept;
}

void
tcpm_usb_start(tcpm_t *tcpm, uint8_t port)
{
  uint32_t primask = tcpm_lock();
  tcpm_update_usb_data_locked(&tcpm->ports[port], port);
  tcpm_unlock(primask);
  tcpm_vdm_start(tcpm, port);
}

void
tcpm_usb_stop(tcpm_t *tcpm, uint8_t port)
{
  uint32_t primask = tcpm_lock();
  tcpm_reset_modes_locked(&tcpm->ports[port], port);
  usb_set_data((usb_t)port, USB_DATA_NONE, USB_DATA_NONE, USB_DATA_NONE);
  tcpm_unlock(primask);
}

tcpm_sgmii_status_t
tcpm_get_sgmii_status(tcpm_t *tcpm, uint8_t port)
{
  uint32_t primask = tcpm_lock();
  tcpm_sgmii_status_t status = tcpm->ports[port].sgmii;
  tcpm_unlock(primask);
  return status;
}

const uint16_t *
tcpm_get_svids(tcpm_t *tcpm, uint8_t port, uint8_t *count)
{
  *count = port == TCPM_PORT_1 ? sizeof(svids) / sizeof(svids[0]) : 0U;
  return svids;
}

const uint32_t *
tcpm_get_modes(tcpm_t *tcpm, uint8_t port, uint16_t svid, uint8_t *count)
{
  *count = (port == TCPM_PORT_1
            && (svid == TCPM_POWER_ORING_SVID || svid == TCPM_SGMII_SVID))
               ? 1
               : 0;
  return &mode_vdo;
}

bool
tcpm_vdm_mode_enter(tcpm_t *tcpm,
                    uint8_t port,
                    uint16_t svid,
                    uint32_t mode_index)
{
  tcpm_port_t *p = &tcpm->ports[port];
  if (port != TCPM_PORT_1 || mode_index != 1)
    {
      return false;
    }
  uint32_t primask = tcpm_lock();
  bool entered = false;
  if (p->attached && p->explicit_contract)
    {
      if (svid == TCPM_POWER_ORING_SVID)
        {
          entered = usb_set_power((usb_t)port, USB_POWER_20V) == USB_SUCCESS;
          p->power_oring_active = entered;
        }
      else if (svid == TCPM_SGMII_SVID)
        {
          if (p->sgmii.active)
            {
              entered = true;
            }
          else if (p->cc != TCPM_CC_NONE)
            {
              p->sgmii.active = true;
              entered = tcpm_update_usb_data_locked(p, port) == USB_SUCCESS;
              p->sgmii.active = entered;
              if (entered)
                {
                  ++p->sgmii.cycles;
                }
            }
        }
    }
  tcpm_unlock(primask);
  return entered;
}

bool
tcpm_vdm_mode_exit(tcpm_t *tcpm,
                   uint8_t port,
                   uint16_t svid,
                   uint32_t mode_index)
{
  if (port != TCPM_PORT_1 || mode_index != 1)
    {
      return false;
    }
  tcpm_port_t *p = &tcpm->ports[port];
  uint32_t primask = tcpm_lock();
  bool exited = true;
  if (svid == TCPM_POWER_ORING_SVID)
    {
      tcpm_stop_power_oring_locked(p, port);
    }
  else if (svid == TCPM_SGMII_SVID)
    {
      tcpm_stop_sgmii_locked(p);
      tcpm_update_usb_data_locked(p, port);
    }
  else
    {
      exited = false;
    }
  tcpm_unlock(primask);
  return exited;
}

static void
tcpm_vdm_send_failed(tcpm_port_t *p, tcpm_vdm_state_t expected)
{
  uint32_t primask = tcpm_lock();
  if (p->vdm_state == expected)
    {
      p->vdm_state = TCPM_VDM_IDLE;
    }
  tcpm_unlock(primask);
}

static void
tcpm_vdm_finish_mode(tcpm_t *tcpm, uint8_t port)
{
  tcpm_port_t *p = &tcpm->ports[port];
  uint32_t primask = tcpm_lock();
  bool next = false;
  switch (p->vdm_state)
    {
    case TCPM_VDM_DISCOVER_POWER_MODES:
    case TCPM_VDM_ENTER_POWER_MODE:
      next = p->partner_has_sgmii && !p->sgmii.active;
      break;
    case TCPM_VDM_DISCOVER_SGMII_MODES:
    case TCPM_VDM_ENTER_SGMII_MODE:
      break;
    default:
      tcpm_unlock(primask);
      return;
    }
  p->vdm_state = next ? TCPM_VDM_DISCOVER_SGMII_MODES : TCPM_VDM_READY;
  tcpm_unlock(primask);
  if (next
      && USBPD_DPM_RequestVDM_DiscoveryMode(
             port, USBPD_SOPTYPE_SOP, TCPM_SGMII_SVID)
             != USBPD_OK)
    {
      primask = tcpm_lock();
      if (p->vdm_state == TCPM_VDM_DISCOVER_SGMII_MODES)
        {
          p->vdm_state = TCPM_VDM_READY;
        }
      tcpm_unlock(primask);
    }
}

static void
tcpm_vdm_start(tcpm_t *tcpm, uint8_t port)
{
  tcpm_port_t *p = &tcpm->ports[port];
  if (port != TCPM_PORT_1)
    {
      return;
    }
  uint32_t primask = tcpm_lock();
  bool start = p->attached && p->explicit_contract && p->data_role == TCPM_HOST
               && p->vdm_state == TCPM_VDM_IDLE;
  if (start)
    {
      p->vdm_state = TCPM_VDM_DISCOVER_IDENTITY;
    }
  tcpm_unlock(primask);
  if (start
      && USBPD_DPM_RequestVDM_DiscoveryIdentify(port, USBPD_SOPTYPE_SOP)
             != USBPD_OK)
    {
      tcpm_vdm_send_failed(p, TCPM_VDM_DISCOVER_IDENTITY);
    }
}

void
tcpm_vdm_identity_result(tcpm_t *tcpm, uint8_t port, bool accepted, bool modal)
{
  tcpm_port_t *p = &tcpm->ports[port];
  uint32_t primask = tcpm_lock();
  if (p->vdm_state != TCPM_VDM_DISCOVER_IDENTITY)
    {
      tcpm_unlock(primask);
      return;
    }
  bool discover = accepted && modal;
  p->vdm_state = discover ? TCPM_VDM_DISCOVER_SVIDS : TCPM_VDM_READY;
  tcpm_unlock(primask);
  if (discover
      && USBPD_DPM_RequestVDM_DiscoverySVID(port, USBPD_SOPTYPE_SOP)
             != USBPD_OK)
    {
      tcpm_vdm_send_failed(p, TCPM_VDM_DISCOVER_SVIDS);
    }
}

void
tcpm_vdm_svids_result(tcpm_t *tcpm,
                      uint8_t port,
                      bool accepted,
                      const uint16_t *svids,
                      uint8_t count)
{
  tcpm_port_t *p = &tcpm->ports[port];
  bool power_oring = false;
  bool sgmii = false;
  if (accepted)
    {
      for (uint8_t i = 0U; i < count; ++i)
        {
          power_oring |= svids[i] == TCPM_POWER_ORING_SVID;
          sgmii |= svids[i] == TCPM_SGMII_SVID;
        }
    }
  uint32_t primask = tcpm_lock();
  if (p->vdm_state != TCPM_VDM_DISCOVER_SVIDS)
    {
      tcpm_unlock(primask);
      return;
    }
  p->partner_has_sgmii = sgmii;
  if (power_oring)
    {
      p->vdm_state = TCPM_VDM_DISCOVER_POWER_MODES;
    }
  else if (sgmii)
    {
      p->vdm_state = TCPM_VDM_DISCOVER_SGMII_MODES;
    }
  else
    {
      p->vdm_state = TCPM_VDM_READY;
    }
  tcpm_unlock(primask);
  uint16_t svid = power_oring ? TCPM_POWER_ORING_SVID : TCPM_SGMII_SVID;
  if ((power_oring || sgmii)
      && USBPD_DPM_RequestVDM_DiscoveryMode(port, USBPD_SOPTYPE_SOP, svid)
             != USBPD_OK)
    {
      tcpm_vdm_finish_mode(tcpm, port);
    }
}

void
tcpm_vdm_modes_result(tcpm_t *tcpm,
                      uint8_t port,
                      bool accepted,
                      uint16_t svid,
                      const uint32_t *modes,
                      uint8_t count)
{
  tcpm_port_t *p = &tcpm->ports[port];
  uint32_t primask = tcpm_lock();
  uint16_t expected_svid = 0U;
  if (p->vdm_state == TCPM_VDM_DISCOVER_POWER_MODES)
    {
      expected_svid = TCPM_POWER_ORING_SVID;
    }
  else if (p->vdm_state == TCPM_VDM_DISCOVER_SGMII_MODES)
    {
      expected_svid = TCPM_SGMII_SVID;
    }
  if (expected_svid == 0U || (svid != 0U && svid != expected_svid))
    {
      tcpm_unlock(primask);
      return;
    }
  bool enter = accepted && count > 0U && modes[0] == mode_vdo;
  if (enter)
    {
      p->vdm_state = expected_svid == TCPM_POWER_ORING_SVID
                         ? TCPM_VDM_ENTER_POWER_MODE
                         : TCPM_VDM_ENTER_SGMII_MODE;
    }
  tcpm_unlock(primask);
  if (!enter
      || USBPD_DPM_RequestVDM_EnterMode(
             port, USBPD_SOPTYPE_SOP, expected_svid, 1)
             != USBPD_OK)
    {
      tcpm_vdm_finish_mode(tcpm, port);
    }
}

void
tcpm_vdm_enter_result(tcpm_t *tcpm,
                      uint8_t port,
                      bool accepted,
                      uint16_t svid,
                      uint32_t mode_index)
{
  tcpm_port_t *p = &tcpm->ports[port];
  uint32_t primask = tcpm_lock();
  bool expected = (svid == TCPM_POWER_ORING_SVID
                   && p->vdm_state == TCPM_VDM_ENTER_POWER_MODE)
                  || (svid == TCPM_SGMII_SVID
                      && p->vdm_state == TCPM_VDM_ENTER_SGMII_MODE);
  if (!expected || mode_index != 1)
    {
      tcpm_unlock(primask);
      return;
    }
  if (accepted)
    {
      tcpm_vdm_mode_enter(tcpm, port, svid, mode_index);
    }
  tcpm_unlock(primask);
  tcpm_vdm_finish_mode(tcpm, port);
}

void
tcpm_vdm_exit_result(tcpm_t *tcpm,
                     uint8_t port,
                     bool accepted,
                     uint16_t svid,
                     uint32_t mode_index)
{
  if (accepted)
    {
      tcpm_vdm_mode_exit(tcpm, port, svid, mode_index);
    }
}
