/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usbpd_vdm_user.c
  * @author  MCD Application Team
  * @brief   USBPD provider demo file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbpd_core.h"
#include "usbpd_dpm_conf.h"
#include "usbpd_vdm_user.h"
#include "usbpd_dpm_user.h"
/* USER CODE BEGIN Includes */
#include <string.h>
#include "usbpd_dpm_core.h"
#include "tcpm.h"
/* USER CODE END Includes */

/** @addtogroup STM32_USBPD_APPLICATION
  * @{
  */

/** @addtogroup STM32_USBPD_APPLICATION_VDM_USER
  * @{
  */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Private_define */

/* USER CODE END Private_define */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN Private_typedef */

/* USER CODE END Private_typedef */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN Private_macro */

/* USER CODE END Private_macro */

/* Private function prototypes -----------------------------------------------*/
static USBPD_StatusTypeDef USBPD_VDM_DiscoverIdentity(uint8_t PortNum, USBPD_DiscoveryIdentity_TypeDef *pIdentity);
static USBPD_StatusTypeDef USBPD_VDM_DiscoverSVIDs(uint8_t PortNum, uint16_t **p_SVID_Info, uint8_t *nb);
static USBPD_StatusTypeDef USBPD_VDM_DiscoverModes(uint8_t PortNum, uint16_t SVID, uint32_t **p_ModeInfo, uint8_t *nbMode);
static USBPD_StatusTypeDef USBPD_VDM_ModeEnter(uint8_t PortNum, uint16_t SVID, uint32_t ModeIndex);
static USBPD_StatusTypeDef USBPD_VDM_ModeExit(uint8_t PortNum, uint16_t SVID, uint32_t ModeIndex);
static void USBPD_VDM_SendAttention(uint8_t PortNum, uint8_t *NbData, uint32_t *VDO);
static void USBPD_VDM_ReceiveAttention(uint8_t PortNum, uint8_t NbData, uint32_t VDO);
static USBPD_StatusTypeDef USBPD_VDM_ReceiveSpecific(uint8_t PortNum, USBPD_VDM_Command_Typedef VDMCommand, uint8_t *NbData, uint32_t *VDO);
static void USBPD_VDM_InformIdentity(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_CommandType_Typedef CommandStatus, USBPD_DiscoveryIdentity_TypeDef *pIdentity);
static void USBPD_VDM_InformSVID(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_CommandType_Typedef CommandStatus, USBPD_SVIDInfo_TypeDef *pListSVID);
static void USBPD_VDM_InformMode(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_CommandType_Typedef CommandStatus, USBPD_ModeInfo_TypeDef *pModesInfo);
static void USBPD_VDM_InformModeEnter(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_CommandType_Typedef CommandStatus, uint16_t SVID, uint32_t ModeIndex);
static void USBPD_VDM_InformModeExit(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_CommandType_Typedef CommandStatus, uint16_t SVID, uint32_t ModeIndex);
static void USBPD_VDM_InformSpecific(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_Command_Typedef VDMCommand, uint8_t *NbData, uint32_t *VDO);
static void USBPD_VDM_SendSpecific(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_Command_Typedef VDMCommand, uint8_t *NbData, uint32_t *VDO);
static void USBPD_VDM_SendUVDM(uint8_t PortNum, USBPD_UVDMHeader_TypeDef *pUVDM_Header, uint8_t *pNbData, uint32_t *pVDO);
static USBPD_StatusTypeDef USBPD_VDM_ReceiveUVDM(uint8_t PortNum, USBPD_UVDMHeader_TypeDef UVDM_Header, uint8_t *pNbData, uint32_t *pVDO);

/* USER CODE BEGIN Private_prototypes */

/* USER CODE END Private_prototypes */

/* Private variables ---------------------------------------------------------*/
USBPD_VDM_SettingsTypeDef DPM_VDM_Settings[USBPD_PORT_COUNT] =
{
  {
    .VDM_XID_SOP                = USBPD_XID,    /*!< A decimal number assigned by USB-IF before certification */
    .VDM_USB_VID_SOP            = USBPD_VID,    /*!< A decimal number assigned by USB-IF before certification */
    .VDM_PID_SOP                = USBPD_PID,    /*!< A unique number assigned by the Vendor ID holder identifying the product. */
    .VDM_ModalOperation         = MODAL_OPERATION_NONSUPP, /*!< Product support Modes based on @ref USBPD_ModalOp_TypeDef */
    .VDM_bcdDevice_SOP          = 0xAAAA,        /*!< A unique number assigned by the Vendor ID holder containing identity information relevant to the release version of the product. */
    .VDM_USBHostSupport         = USB_NOTCAPABLE, /*!< Indicates whether the UUT is capable of enumerating USB Host */
    .VDM_USBDeviceSupport       = USB_NOTCAPABLE, /*!< Indicates whether the UUT is capable of enumerating USB Devices */
    .VDM_ProductTypeUFPorCP     = PRODUCT_TYPE_UNDEFINED, /*!< Product type UFP or CablePlug of the UUT based on @ref USBPD_ProductType_TypeDef */
  },
  {
    .VDM_XID_SOP                = USBPD_XID,    /* A decimal number assigned by USB-IF before certification */
    .VDM_USB_VID_SOP            = USBPD_VID,    /* A decimal number assigned by USB-IF before certification */
    .VDM_PID_SOP                = USBPD_PID,    /*!< A unique number assigned by the Vendor ID holder identifying the product. */
    .VDM_ModalOperation         = MODAL_OPERATION_SUPPORTED, /*!< Product support Modes based on @ref USBPD_ModalOp_TypeDef */
    .VDM_bcdDevice_SOP          = 0xAAAA,        /*!< A unique number assigned by the Vendor ID holder containing identity information relevant to the release version of the product. */
    .VDM_USBHostSupport         = USB_CAPABLE, /*!< Indicates whether the UUT is capable of enumerating USB Host */
    .VDM_USBDeviceSupport       = USB_NOTCAPABLE, /*!< Indicates whether the UUT is capable of enumerating USB Devices */
    .VDM_ProductTypeUFPorCP     = PRODUCT_TYPE_PERIPHERAL, /*!< Product type UFP or CablePlug of the UUT based on @ref USBPD_ProductType_TypeDef */
    .VDM_ProductTypeDFP         = PRODUCT_TYPE_HOST,
  }
};

/* USER CODE BEGIN Private_variables */
USBPD_VDM_Callbacks vdmCallbacks =
{
  USBPD_VDM_DiscoverIdentity,
  USBPD_VDM_DiscoverSVIDs,
  USBPD_VDM_DiscoverModes,
  USBPD_VDM_ModeEnter,
  USBPD_VDM_ModeExit,
  USBPD_VDM_InformIdentity,
  USBPD_VDM_InformSVID,
  USBPD_VDM_InformMode,
  USBPD_VDM_InformModeEnter,
  USBPD_VDM_InformModeExit,
  USBPD_VDM_SendAttention,
  USBPD_VDM_ReceiveAttention,
  USBPD_VDM_SendSpecific,
  USBPD_VDM_ReceiveSpecific,
  USBPD_VDM_InformSpecific,
  USBPD_VDM_SendUVDM,
  USBPD_VDM_ReceiveUVDM,
};
/* USER CODE END Private_variables */

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  VDM Discovery identity callback
  * @note   Function is called to get Discovery identity information linked to the device and answer
  *         to SVDM Discovery identity init message sent by port partner
  * @param  PortNum current port number
  * @param  pIdentity Pointer on @ref USBPD_DiscoveryIdentity_TypeDef structure
  * @retval USBPD status: @ref USBPD_ACK or @ref USBPD_BUSY
  */
static USBPD_StatusTypeDef USBPD_VDM_DiscoverIdentity(uint8_t PortNum, USBPD_DiscoveryIdentity_TypeDef *pIdentity)
{
/* USER CODE BEGIN USBPD_VDM_DiscoverIdentity */
  if ((USBPD_PORT_1 != PortNum) || (NULL == pIdentity))
  {
    return USBPD_NAK;
  }

  const USBPD_VDM_SettingsTypeDef *settings = &DPM_VDM_Settings[PortNum];

  memset(pIdentity, 0, sizeof(*pIdentity));
  pIdentity->IDHeader.b20.VID = settings->VDM_USB_VID_SOP;
  pIdentity->IDHeader.b20.ModalOperation = settings->VDM_ModalOperation;
  pIdentity->IDHeader.b20.ProductTypeUFPorCP = settings->VDM_ProductTypeUFPorCP;
  pIdentity->IDHeader.b20.USBDevCapability = settings->VDM_USBDeviceSupport;
  pIdentity->IDHeader.b20.USBHostCapability = settings->VDM_USBHostSupport;
  pIdentity->CertStatVDO.b.XID = settings->VDM_XID_SOP;
  pIdentity->ProductVDO.b.USBProductId = settings->VDM_PID_SOP;
  pIdentity->ProductVDO.b.bcdDevice = settings->VDM_bcdDevice_SOP;

  if ((USBPD_SPECIFICATION_REV3 == DPM_Params[PortNum].PE_SpecRevision)
      && (USBPD_VDMVERSION_REV2 == DPM_Params[PortNum].VDM_Version))
  {
    pIdentity->IDHeader.b30.ConnectorType = CONNECTOR_TYPE_C_RECEPTACLE;
    pIdentity->IDHeader.b30.ProductTypeDFP = settings->VDM_ProductTypeDFP;

    /* UFP operation supports alternate modes, but no USB device communication. */
    pIdentity->UFP_VDO.b.UFPVDOVersion = USBPD_VDM_VDO_UFP_VERSION_REV1P1;
    pIdentity->UFP_VDO.b.ConnectorType = CONNECTOR_TYPE_C_RECEPTACLE;
    pIdentity->UFP_VDO.b.DeviceCapability = 0U;
    pIdentity->UFP_VDO.b.USB_HighestSpeed = USB2P0_ONLY;
    pIdentity->UFP_VDO.b.AlternateModes = (1U << ALTERNATE_MODES_RECONFIG_TYPEC_2P0) | (1U << ALTERNATE_MODES_NO_RECONFIG_TYPEC_2P0);
    pIdentity->UFP_VDO_Presence = 1U;

    pIdentity->DFP_VDO.b.DFPVDOVersion = USBPD_VDM_VDO_DFP_VERSION_REV1P1;
    pIdentity->DFP_VDO.b.ConnectorType = CONNECTOR_TYPE_C_RECEPTACLE;
    pIdentity->DFP_VDO.b.HostCapability = (1U << HOST_CAPABILITY_USB2P0) | (1U << HOST_CAPABILITY_USB3P2);
    pIdentity->DFP_VDO.b.PortNumber = PortNum;
    pIdentity->DFP_VDO_Presence = 1U;
  }
  return USBPD_ACK;
/* USER CODE END USBPD_VDM_DiscoverIdentity */
}

/**
  * @brief  VDM Discover SVID callback
  * @note   Function is called to retrieve SVID supported by device and answer
  *         to SVDM Discovery SVID init message sent by port partner
  * @param  PortNum        current port number
  * @param  p_SVID_Info Pointer on @ref USBPD_SVIDInfo_TypeDef structure
  * @param  pNbSVID     Pointer on number of SVID
  * @retval USBPD status  @ref USBPD_BUSY or @ref USBPD_ACK or @ref USBPD_NAK
  */
static USBPD_StatusTypeDef USBPD_VDM_DiscoverSVIDs(uint8_t PortNum, uint16_t **p_SVID_Info, uint8_t *pNbSVID)
{
/* USER CODE BEGIN USBPD_VDM_DiscoverSVIDs */
  *p_SVID_Info = (uint16_t *)tcpm_get_svids(tcpm, PortNum, pNbSVID);
  return (0U != *pNbSVID) ? USBPD_ACK : USBPD_NAK;
/* USER CODE END USBPD_VDM_DiscoverSVIDs */
}

/**
  * @brief  VDM Discover Mode callback (report all the modes supported by SVID)
  * @note   Function is called to report all the modes supported by selected SVID and answer
  *         to SVDM Discovery Mode init message sent by port partner
  * @param  PortNum current port number
  * @param  SVID         SVID value
  * @param  p_ModeTab    Pointer on the mode value
  * @param  NumberOfMode Number of mode available
  * @retval USBPD status
  */
static USBPD_StatusTypeDef USBPD_VDM_DiscoverModes(uint8_t PortNum, uint16_t SVID, uint32_t **p_ModeTab, uint8_t *NumberOfMode)
{
/* USER CODE BEGIN USBPD_VDM_DiscoverModes */
  *p_ModeTab = (uint32_t *)tcpm_get_modes(tcpm, PortNum, SVID, NumberOfMode);
  return (0U != *NumberOfMode) ? USBPD_ACK : USBPD_NAK;
/* USER CODE END USBPD_VDM_DiscoverModes */
}

/**
  * @brief  VDM Mode enter callback
  * @note   Function is called to check if device can enter in the mode received for the selected SVID in the
  *         SVDM enter mode init message sent by port partner
  * @param  PortNum current port number
  * @param  SVID      SVID value
  * @param  ModeIndex Index of the mode to be entered
  * @retval USBPD status @ref USBPD_ACK/@ref USBPD_NAK
  */
static USBPD_StatusTypeDef USBPD_VDM_ModeEnter(uint8_t PortNum, uint16_t SVID, uint32_t ModeIndex)
{
/* USER CODE BEGIN USBPD_VDM_ModeEnter */
  return tcpm_vdm_mode_enter(tcpm, PortNum, SVID, ModeIndex) ? USBPD_ACK : USBPD_NAK;
/* USER CODE END USBPD_VDM_ModeEnter */
}

/**
  * @brief  VDM Mode exit callback
  * @note   Function is called to check if device can exit from the mode received for the selected SVID in the
  *         SVDM exit mode init message sent by port partner
  * @param  PortNum current port number
  * @param  SVID      SVID value
  * @param  ModeIndex Index of the mode to be exited
  * @retval USBPD status @ref USBPD_ACK/@ref USBPD_NAK
  */
static USBPD_StatusTypeDef USBPD_VDM_ModeExit(uint8_t PortNum, uint16_t SVID, uint32_t ModeIndex)
{
/* USER CODE BEGIN USBPD_VDM_ModeExit */
  return tcpm_vdm_mode_exit(tcpm, PortNum, SVID, ModeIndex) ? USBPD_ACK : USBPD_NAK;
/* USER CODE END USBPD_VDM_ModeExit */
}

/**
  * @brief  Send VDM Attention message callback
  * @note   Function is called when device wants to send a SVDM attention message to port partner
  *         (for instance DP status can be filled through this function)
  * @param  PortNum    current port number
  * @param  pNbData    Pointer of number of VDO to send
  * @param  pVDO       Pointer of VDO to send
  * @retval None
  */
static void USBPD_VDM_SendAttention(uint8_t PortNum, uint8_t *pNbData, uint32_t *pVDO)
{
/* USER CODE BEGIN USBPD_VDM_SendAttention */

/* USER CODE END USBPD_VDM_SendAttention */
}

/**
  * @brief  Receive VDM Attention callback
  * @note   Function is called when a SVDM attention init message has been received from port partner
  *         (for instance, save DP status data through this function)
  * @param  PortNum   current port number
  * @param  NbData    Number of received VDO
  * @param  VDO       Received VDO
  * @retval None
  */
static void USBPD_VDM_ReceiveAttention(uint8_t PortNum, uint8_t NbData, uint32_t VDO)
{
/* USER CODE BEGIN USBPD_VDM_ReceiveAttention */

/* USER CODE END USBPD_VDM_ReceiveAttention */
}

/**
  * @brief  VDM Receive Specific message callback
  * @note   Function is called to answer to a SVDM specific init message received by port partner.
  *         (for instance, retrieve DP status or DP configure data through this function)
  * @param  PortNum     Current port number
  * @param  VDMCommand  VDM command based on @ref USBPD_VDM_Command_Typedef
  * @param  pNbData     Pointer of number of received VDO and used for the answer
  * @param  pVDO        Pointer of received VDO and use for the answer
  * @retval USBPD Status
  */
static USBPD_StatusTypeDef USBPD_VDM_ReceiveSpecific(uint8_t PortNum, USBPD_VDM_Command_Typedef VDMCommand, uint8_t *pNbData, uint32_t *pVDO)
{
/* USER CODE BEGIN USBPD_VDM_ReceiveSpecific */
  return USBPD_NAK;
/* USER CODE END USBPD_VDM_ReceiveSpecific */
}

/**
  * @brief  Inform identity callback
  * @note   Function is called to save Identity information received in Discovery identity from port partner
            (answer to SVDM discovery identity sent by device)
  * @param  PortNum       current port number
  * @param  SOPType       SOP type
  * @param  CommandStatus Command status based on @ref USBPD_VDM_CommandType_Typedef
  * @param  pIdentity     Pointer on the discovery identity information based on @ref USBPD_DiscoveryIdentity_TypeDef
  * @retval None
*/
static void USBPD_VDM_InformIdentity(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_CommandType_Typedef CommandStatus, USBPD_DiscoveryIdentity_TypeDef *pIdentity)
{
/* USER CODE BEGIN USBPD_VDM_InformIdentity */
  tcpm_vdm_identity_result(
    tcpm, PortNum,
    (USBPD_SOPTYPE_SOP == SOPType) && (SVDM_RESPONDER_ACK == CommandStatus) && (NULL != pIdentity),
    (NULL != pIdentity) && (MODAL_OPERATION_SUPPORTED == pIdentity->IDHeader.b30.ModalOperation));
/* USER CODE END USBPD_VDM_InformIdentity */
}

/**
  * @brief  Inform SVID callback
  * @note   Function is called to save list of SVID received in Discovery SVID from port partner
            (answer to SVDM discovery SVID sent by device)
  * @param  PortNum       current port number
  * @param  SOPType       SOP type
  * @param  CommandStatus Command status based on @ref USBPD_VDM_CommandType_Typedef
  * @param  pListSVID     Pointer of list of SVID based on @ref USBPD_SVIDInfo_TypeDef
  * @retval None
  */
static void USBPD_VDM_InformSVID(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_CommandType_Typedef CommandStatus, USBPD_SVIDInfo_TypeDef *pListSVID)
{
/* USER CODE BEGIN USBPD_VDM_InformSVID */
  bool accepted = (USBPD_SOPTYPE_SOP == SOPType)
                  && (SVDM_RESPONDER_ACK == CommandStatus)
                  && (NULL != pListSVID)
                  && (pListSVID->NumSVIDs <= sizeof(pListSVID->SVIDs) / sizeof(pListSVID->SVIDs[0]));

  tcpm_vdm_svids_result(tcpm, PortNum, accepted,
                         accepted ? pListSVID->SVIDs : NULL,
                         accepted ? pListSVID->NumSVIDs : 0U);
/* USER CODE END USBPD_VDM_InformSVID */
}

/**
  * @brief  Inform Mode callback ( received in Discovery Modes ACK)
  * @note   Function is called to save list of modes linked to SVID received in Discovery mode from port partner
            (answer to SVDM discovery mode sent by device)
  * @param  PortNum         current port number
  * @param  SOPType         SOP type
  * @param  CommandStatus   Command status based on @ref USBPD_VDM_CommandType_Typedef
  * @param  pModesInfo      Pointer of Modes info based on @ref USBPD_ModeInfo_TypeDef
  * @retval None
  */
static void USBPD_VDM_InformMode(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_CommandType_Typedef CommandStatus, USBPD_ModeInfo_TypeDef *pModesInfo)
{
/* USER CODE BEGIN USBPD_VDM_InformMode */
  bool accepted = (USBPD_SOPTYPE_SOP == SOPType)
                  && (SVDM_RESPONDER_ACK == CommandStatus)
                  && (NULL != pModesInfo);

  if (USBPD_SOPTYPE_SOP == SOPType)
  {
    tcpm_vdm_modes_result(tcpm, PortNum, accepted,
                           (NULL != pModesInfo) ? pModesInfo->SVID : 0U,
                           accepted ? pModesInfo->Modes : NULL,
                           accepted ? pModesInfo->NumModes : 0U);
  }
/* USER CODE END USBPD_VDM_InformMode */
}

/**
  * @brief  Inform Mode enter callback
  * @note   Function is called to inform if port partner accepted or not to enter in the mode
  *         specified in the SVDM enter mode sent by the device
  * @param  PortNum   current port number
  * @param  SOPType       SOP type
  * @param  CommandStatus Command status based on @ref USBPD_VDM_CommandType_Typedef
  * @param  SVID      SVID ID
  * @param  ModeIndex Index of the mode to be entered
  * @retval None
  */
static void USBPD_VDM_InformModeEnter(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_CommandType_Typedef CommandStatus, uint16_t SVID, uint32_t ModeIndex)
{
/* USER CODE BEGIN USBPD_VDM_InformModeEnter */
  tcpm_vdm_enter_result(tcpm, PortNum,
                         (USBPD_SOPTYPE_SOP == SOPType)
                           && (SVDM_RESPONDER_ACK == CommandStatus),
                         SVID, ModeIndex);
/* USER CODE END USBPD_VDM_InformModeEnter */
}

/**
  * @brief  Inform Exit enter callback
  * @param  PortNum   current port number
  * @param  SVID      SVID ID
  * @param  ModeIndex Index of the mode to be entered
  * @retval None
  */
static void USBPD_VDM_InformModeExit(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_CommandType_Typedef CommandStatus, uint16_t SVID, uint32_t ModeIndex)
{
/* USER CODE BEGIN USBPD_VDM_InformModeExit */
  tcpm_vdm_exit_result(tcpm, PortNum,
                        (USBPD_SOPTYPE_SOP == SOPType)
                          && (SVDM_RESPONDER_ACK == CommandStatus),
                        SVID, ModeIndex);
/* USER CODE END USBPD_VDM_InformModeExit */
}

/**
  * @brief  VDM Send Specific message callback
  * @note   Function is called when device wants to send a SVDM specific init message to port partner
  *         (for instance DP status or DP configure can be filled through this function)
  * @param  PortNum    current port number
  * @param  SOPType    SOP type
  * @param  VDMCommand VDM command based on @ref USBPD_VDM_Command_Typedef
  * @param  pNbData    Pointer of number of VDO to send
  * @param  pVDO       Pointer of VDO to send
  * @retval None
  */
static void USBPD_VDM_SendSpecific(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_Command_Typedef VDMCommand, uint8_t *pNbData, uint32_t *pVDO)
{
/* USER CODE BEGIN USBPD_VDM_SendSpecific */

/* USER CODE END USBPD_VDM_SendSpecific */
}

/**
  * @brief  VDM Specific message callback to inform user of reception of VDM specific message
  * @note   Function is called when answer from SVDM specific init message has been received by the device
  *         (for instance, save DP status and DP configure data through this function)
  * @param  PortNum    current port number
  * @param  SOPType    SOP type
  * @param  VDMCommand VDM command based on @ref USBPD_VDM_Command_Typedef
  * @param  pNbData    Pointer of number of received VDO
  * @param  pVDO       Pointer of received VDO
  * @retval None
  */
static void USBPD_VDM_InformSpecific(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType, USBPD_VDM_Command_Typedef VDMCommand, uint8_t *pNbData, uint32_t *pVDO)
{
/* USER CODE BEGIN USBPD_VDM_InformSpecific */

/* USER CODE END USBPD_VDM_InformSpecific */
}

/**
  * @brief  VDM Send Unstructured message callback
  * @note   Aim of this function is to fill the UVDM message which contains 1 VDM Header + 6 VDO
  *         This callback will be called when user requests to send a UVDM message thanks
  *         to USBPD_DPM_RequestUVDMMessage function
  * @param  PortNum       current port number
  * @param  pUVDM_Header  Pointer on UVDM header based on @ref USBPD_UVDMHeader_TypeDef
  * @param  pNbData       Pointer of number of VDO to send (max size must be equal to 6)
  * @param  pVDO          Pointer of VDO to send (up to 6 x uint32_t)
  * @retval None
  */
static void USBPD_VDM_SendUVDM(uint8_t PortNum, USBPD_UVDMHeader_TypeDef *pUVDM_Header, uint8_t *pNbData, uint32_t *pVDO)
{
/* USER CODE BEGIN USBPD_VDM_SendUVDM */

/* USER CODE END USBPD_VDM_SendUVDM */
}

/**
  * @brief  Unstructured VDM  message callback to inform user of reception of UVDM message
  * @param  PortNum    current port number
  * @param  UVDM_Header UVDM header based on @ref USBPD_UVDMHeader_TypeDef
  * @param  pNbData    Pointer of number of received VDO
  * @param  pVDO       Pointer of received VDO
  * @retval USBPD Status
  */
static USBPD_StatusTypeDef USBPD_VDM_ReceiveUVDM(uint8_t PortNum, USBPD_UVDMHeader_TypeDef UVDM_Header, uint8_t *pNbData, uint32_t *pVDO)
{
/* USER CODE BEGIN USBPD_VDM_ReceiveUVDM */
  return USBPD_ERROR;
/* USER CODE END USBPD_VDM_ReceiveUVDM */
}

/* USER CODE BEGIN Private_functions */

/* USER CODE END Private_functions */

/* Exported functions ---------------------------------------------------------*/
/**
  * @brief  VDM Initialization function
  * @param  PortNum     Index of current used port
  * @retval status
  */
USBPD_StatusTypeDef USBPD_VDM_UserInit(uint8_t PortNum)
{
/* USER CODE BEGIN USBPD_VDM_UserInit */
  if (PortNum >= USBPD_PORT_COUNT)
  {
    return USBPD_ERROR;
  }

  if (USBPD_PORT_1 == PortNum)
  {
    USBPD_PE_InitVDM_Callback(PortNum, &vdmCallbacks);
  }

  return USBPD_OK;
/* USER CODE END USBPD_VDM_UserInit */
}

/**
  * @brief  VDM Reset function
  * @param  PortNum     Index of current used port
  * @retval status
  */
void USBPD_VDM_UserReset(uint8_t PortNum)
{
/* USER CODE BEGIN USBPD_VDM_UserReset */

/* USER CODE END USBPD_VDM_UserReset */
}

/* USER CODE BEGIN Exported_functions */

/* USER CODE END Exported_functions */

/**
  * @}
  */

/**
  * @}
  */

