/**
  ******************************************************************************
  * @file    py32f071_ll_flash.h
  * @author  MCU Application Team
  * @brief   Header file of FLASH LL module.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2023 Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by Puya under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef PY32F071_LL_FLASH_H
#define PY32F071_LL_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "py32f0xx.h"



/** @addtogroup PY32F071_LL_Driver
  * @{
  */

#if defined (FLASH)

/** @defgroup FLASH_LL FLASH
  * @{
  */

/* Private types -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private macros ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/

/** @defgroup FLASH_LL_Exported_Constants FLASH Exported Constants
  * @{
  */

/** @defgroup FLASH_LL_EC_nBOOT1 nBOOT1 Set
  * @{
  */
#define LL_FLASH_NBOOT1_CLR      (0x00000000U)
#define LL_FLASH_NBOOT1_SET      (FLASH_OPTR_nBOOT1)
/**
  * @}
  */

/** @defgroup FLASH_LL_EC_NRST Nrst Mode
  * @{
  */
#define LL_FLASH_NRST_MODE_RESET       (0x00000000U)          /*!< PF2:NRST */
#define LL_FLASH_NRST_MODE_GPIO        (FLASH_OPTR_NRST_MODE) /*!< PF2:GPIO */
/**
  * @}
  */

/** @defgroup FLASH_LL_EC_WWDGMODE WWDG Mode
  * @{
  */
#define LL_FLASH_WWDG_MODE_HW          (0x00000000U)         /*!< Hardware WWDG selected */
#define LL_FLASH_WWDG_MODE_SW          (FLASH_OPTR_WWDG_SW)  /*!< Software WWDG selected */

/**
  * @}
  */

/** @defgroup FLASH_LL_EC_IWDGMODE IWDG Mode
  * @{
  */
#define LL_FLASH_IWDG_MODE_HW          (0x00000000U)         /*!< Hardware IWDG selected */
#define LL_FLASH_IWDG_MODE_SW          (FLASH_OPTR_IWDG_SW)  /*!< Software IWDG selected */

/**
  * @}
  */

/** @defgroup FLASH_LL_EC_IWDGINSTOP IWDG In Stop
  * @{
  */
#define LL_FLASH_IWDG_INSTOP_FREEZE    (0x00000000U)          /*!< Freeze IWDG counter in STOP mode */
#define LL_FLASH_IWDG_INSTOP_ACTIVE    (FLASH_OPTR_IWDG_STOP) /*!< Active IWDG counter in STOP mode */

/**
  * @}
  */

/** @defgroup FLASH_LL_EC_RDP RDP Mode
  * @{
  */
#define LL_FLASH_RDP_LEVEL_0         ((uint8_t)0xAAU)
#define LL_FLASH_RDP_LEVEL_1         ((uint8_t)0x55U)

/**
  * @}
  */

/** @defgroup FLASH_LL_EC_WRP Write Protection
  * @{
  */
#define LL_FLASH_WRP_DISABLE           ((uint32_t)0x00000000)        /* Write protection disable     */
#define LL_FLASH_WRP_SECTOR_0          ((uint32_t)FLASH_WRPR_WRP_0)  /* Write protection of Sector0  */
#define LL_FLASH_WRP_SECTOR_1          ((uint32_t)FLASH_WRPR_WRP_1)  /* Write protection of Sector1  */
#define LL_FLASH_WRP_SECTOR_2          ((uint32_t)FLASH_WRPR_WRP_2)  /* Write protection of Sector2  */
#define LL_FLASH_WRP_SECTOR_3          ((uint32_t)FLASH_WRPR_WRP_3)  /* Write protection of Sector3  */
#define LL_FLASH_WRP_SECTOR_4          ((uint32_t)FLASH_WRPR_WRP_4)  /* Write protection of Sector4  */
#define LL_FLASH_WRP_SECTOR_5          ((uint32_t)FLASH_WRPR_WRP_5)  /* Write protection of Sector5  */
#define LL_FLASH_WRP_SECTOR_6          ((uint32_t)FLASH_WRPR_WRP_6)  /* Write protection of Sector6  */
#define LL_FLASH_WRP_SECTOR_7          ((uint32_t)FLASH_WRPR_WRP_7)  /* Write protection of Sector7  */
#define LL_FLASH_WRP_SECTOR_8          ((uint32_t)FLASH_WRPR_WRP_8)  /* Write protection of Sector8  */
#define LL_FLASH_WRP_SECTOR_9          ((uint32_t)FLASH_WRPR_WRP_9)  /* Write protection of Sector9  */
#define LL_FLASH_WRP_SECTOR_10         ((uint32_t)FLASH_WRPR_WRP_10) /* Write protection of Sector10 */
#define LL_FLASH_WRP_SECTOR_11         ((uint32_t)FLASH_WRPR_WRP_11) /* Write protection of Sector11 */
#define LL_FLASH_WRP_SECTOR_12         ((uint32_t)FLASH_WRPR_WRP_12) /* Write protection of Sector12 */
#define LL_FLASH_WRP_SECTOR_13         ((uint32_t)FLASH_WRPR_WRP_13) /* Write protection of Sector13 */
#define LL_FLASH_WRP_SECTOR_14         ((uint32_t)FLASH_WRPR_WRP_14) /* Write protection of Sector14 */
#define LL_FLASH_WRP_SECTOR_15         ((uint32_t)FLASH_WRPR_WRP_15) /* Write protection of Sector15 */

#define LL_FLASH_WRP_Pages0to31        ((uint32_t)FLASH_WRPR_WRP_0)  /* Write protection from page0   to page31 */
#define LL_FLASH_WRP_Pages32to63       ((uint32_t)FLASH_WRPR_WRP_1)  /* Write protection from page32  to page63 */
#define LL_FLASH_WRP_Pages64to95       ((uint32_t)FLASH_WRPR_WRP_2)  /* Write protection from page64  to page95 */
#define LL_FLASH_WRP_Pages96to127      ((uint32_t)FLASH_WRPR_WRP_3)  /* Write protection from page96  to page127 */
#define LL_FLASH_WRP_Pages128to159     ((uint32_t)FLASH_WRPR_WRP_4)  /* Write protection from page128 to page159 */
#define LL_FLASH_WRP_Pages160to191     ((uint32_t)FLASH_WRPR_WRP_5)  /* Write protection from page160 to page191 */
#define LL_FLASH_WRP_Pages192to223     ((uint32_t)FLASH_WRPR_WRP_6)  /* Write protection from page192 to page223 */
#define LL_FLASH_WRP_Pages224to255     ((uint32_t)FLASH_WRPR_WRP_7)  /* Write protection from page224 to page255 */
#define LL_FLASH_WRP_Pages256to287     ((uint32_t)FLASH_WRPR_WRP_8)  /* Write protection from page256 to page287 */
#define LL_FLASH_WRP_Pages288to319     ((uint32_t)FLASH_WRPR_WRP_9)  /* Write protection from page288 to page319 */
#define LL_FLASH_WRP_Pages320to351     ((uint32_t)FLASH_WRPR_WRP_10) /* Write protection from page320 to page351 */
#define LL_FLASH_WRP_Pages352to383     ((uint32_t)FLASH_WRPR_WRP_11) /* Write protection from page352 to page383 */
#define LL_FLASH_WRP_Pages384to415     ((uint32_t)FLASH_WRPR_WRP_12) /* Write protection from page384 to page415 */
#define LL_FLASH_WRP_Pages416to447     ((uint32_t)FLASH_WRPR_WRP_13) /* Write protection from page416 to page447 */
#define LL_FLASH_WRP_Pages448to479     ((uint32_t)FLASH_WRPR_WRP_14) /* Write protection from page448 to page479 */
#define LL_FLASH_WRP_Pages480to511     ((uint32_t)FLASH_WRPR_WRP_15) /* Write protection from page480 to page511 */

#define LL_FLASH_WRP_AllPages          ((uint32_t)0x000000FFU)      /*!< Write protection of all Sectors */
/**
  * @}
  */

/** @defgroup FLASH_LL_EC_SDK SDK Address
  * @{
  */
#define LL_FLASH_SDKSTARTADDRESS_0X08000000   (0x00000000)
#define LL_FLASH_SDKSTARTADDRESS_0X08000800   (                                                                                                FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X08001000   (                                                                        FLASH_SDKR_SDK_STRT_1                        )
#define LL_FLASH_SDKSTARTADDRESS_0X08001800   (                                                                        FLASH_SDKR_SDK_STRT_1 | FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X08002000   (                                                FLASH_SDKR_SDK_STRT_2                                                )
#define LL_FLASH_SDKSTARTADDRESS_0X08002800   (                                                FLASH_SDKR_SDK_STRT_2                         | FLASH_SDKR_SDK_STRT_0) 
#define LL_FLASH_SDKSTARTADDRESS_0X08003000   (                                                FLASH_SDKR_SDK_STRT_2 | FLASH_SDKR_SDK_STRT_1                        )
#define LL_FLASH_SDKSTARTADDRESS_0X08003800   (                                                FLASH_SDKR_SDK_STRT_2 | FLASH_SDKR_SDK_STRT_1 | FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X08004000   (                        FLASH_SDKR_SDK_STRT_3                                                                        )
#define LL_FLASH_SDKSTARTADDRESS_0X08004800   (                        FLASH_SDKR_SDK_STRT_3                                                 | FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X08005000   (                        FLASH_SDKR_SDK_STRT_3                         | FLASH_SDKR_SDK_STRT_1                        )
#define LL_FLASH_SDKSTARTADDRESS_0X08005800   (                        FLASH_SDKR_SDK_STRT_3                         | FLASH_SDKR_SDK_STRT_1 | FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X08006000   (                        FLASH_SDKR_SDK_STRT_3 | FLASH_SDKR_SDK_STRT_2 |                                              )
#define LL_FLASH_SDKSTARTADDRESS_0X08006800   (                        FLASH_SDKR_SDK_STRT_3 | FLASH_SDKR_SDK_STRT_2 |                       | FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X08007000   (                        FLASH_SDKR_SDK_STRT_3 | FLASH_SDKR_SDK_STRT_2 | FLASH_SDKR_SDK_STRT_1                        )
#define LL_FLASH_SDKSTARTADDRESS_0X08007800   (                        FLASH_SDKR_SDK_STRT_3 | FLASH_SDKR_SDK_STRT_2 | FLASH_SDKR_SDK_STRT_1 | FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X08008000   (FLASH_SDKR_SDK_STRT_4                                                                                                )
#define LL_FLASH_SDKSTARTADDRESS_0X08008800   (FLASH_SDKR_SDK_STRT_4                                                                         | FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X08009000   (FLASH_SDKR_SDK_STRT_4                                                 | FLASH_SDKR_SDK_STRT_1                        )
#define LL_FLASH_SDKSTARTADDRESS_0X08009800   (FLASH_SDKR_SDK_STRT_4                                                 | FLASH_SDKR_SDK_STRT_1 | FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X0800A000   (FLASH_SDKR_SDK_STRT_4                         | FLASH_SDKR_SDK_STRT_2                                                )
#define LL_FLASH_SDKSTARTADDRESS_0X0800A800   (FLASH_SDKR_SDK_STRT_4                         | FLASH_SDKR_SDK_STRT_2                         | FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X0800B000   (FLASH_SDKR_SDK_STRT_4                         | FLASH_SDKR_SDK_STRT_2 | FLASH_SDKR_SDK_STRT_1                        )
#define LL_FLASH_SDKSTARTADDRESS_0X0800B800   (FLASH_SDKR_SDK_STRT_4                         | FLASH_SDKR_SDK_STRT_2 | FLASH_SDKR_SDK_STRT_1 | FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X0800C000   (FLASH_SDKR_SDK_STRT_4 | FLASH_SDKR_SDK_STRT_3                                                                        )
#define LL_FLASH_SDKSTARTADDRESS_0X0800C800   (FLASH_SDKR_SDK_STRT_4 | FLASH_SDKR_SDK_STRT_3                                                   FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X0800D000   (FLASH_SDKR_SDK_STRT_4 | FLASH_SDKR_SDK_STRT_3                         | FLASH_SDKR_SDK_STRT_1                        )
#define LL_FLASH_SDKSTARTADDRESS_0X0800D800   (FLASH_SDKR_SDK_STRT_4 | FLASH_SDKR_SDK_STRT_3                         | FLASH_SDKR_SDK_STRT_1 | FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X0800E000   (FLASH_SDKR_SDK_STRT_4 | FLASH_SDKR_SDK_STRT_3 | FLASH_SDKR_SDK_STRT_2                                                )
#define LL_FLASH_SDKSTARTADDRESS_0X0800E800   (FLASH_SDKR_SDK_STRT_4 | FLASH_SDKR_SDK_STRT_3 | FLASH_SDKR_SDK_STRT_2                         | FLASH_SDKR_SDK_STRT_0)
#define LL_FLASH_SDKSTARTADDRESS_0X0800F000   (FLASH_SDKR_SDK_STRT_4 | FLASH_SDKR_SDK_STRT_3 | FLASH_SDKR_SDK_STRT_2 | FLASH_SDKR_SDK_STRT_1                        )
#define LL_FLASH_SDKSTARTADDRESS_0X0800F800   (FLASH_SDKR_SDK_STRT_4 | FLASH_SDKR_SDK_STRT_3 | FLASH_SDKR_SDK_STRT_2 | FLASH_SDKR_SDK_STRT_1 | FLASH_SDKR_SDK_STRT_0)

#define LL_FLASH_SDKENDADDRESS_0X080007FF     (0x00000000)
#define LL_FLASH_SDKENDADDRESS_0X08000FFF     (                                                                                            FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X080017FF     (                                                                     FLASH_SDKR_SDK_END_1                       )
#define LL_FLASH_SDKENDADDRESS_0X08001FFF     (                                                                     FLASH_SDKR_SDK_END_1 | FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X080027FF     (                                              FLASH_SDKR_SDK_END_2                                              )
#define LL_FLASH_SDKENDADDRESS_0X08002FFF     (                                              FLASH_SDKR_SDK_END_2                        | FLASH_SDKR_SDK_END_0) 
#define LL_FLASH_SDKENDADDRESS_0X080037FF     (                                              FLASH_SDKR_SDK_END_2 | FLASH_SDKR_SDK_END_1                       )
#define LL_FLASH_SDKENDADDRESS_0X08003FFF     (                                              FLASH_SDKR_SDK_END_2 | FLASH_SDKR_SDK_END_1 | FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X080047FF     (                       FLASH_SDKR_SDK_END_3                                                                     )
#define LL_FLASH_SDKENDADDRESS_0X08004FFF     (                       FLASH_SDKR_SDK_END_3                                               | FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X080057FF     (                       FLASH_SDKR_SDK_END_3                        | FLASH_SDKR_SDK_END_1                       )
#define LL_FLASH_SDKENDADDRESS_0X08005FFF     (                       FLASH_SDKR_SDK_END_3                        | FLASH_SDKR_SDK_END_1 | FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X080067FF     (                       FLASH_SDKR_SDK_END_3 | FLASH_SDKR_SDK_END_2 |                                            )
#define LL_FLASH_SDKENDADDRESS_0X08006FFF     (                       FLASH_SDKR_SDK_END_3 | FLASH_SDKR_SDK_END_2 |                      | FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X080077FF     (                       FLASH_SDKR_SDK_END_3 | FLASH_SDKR_SDK_END_2 | FLASH_SDKR_SDK_END_1                       )
#define LL_FLASH_SDKENDADDRESS_0X08007FFF     (                       FLASH_SDKR_SDK_END_3 | FLASH_SDKR_SDK_END_2 | FLASH_SDKR_SDK_END_1 | FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X080087FF     (FLASH_SDKR_SDK_END_4                                                                                            )
#define LL_FLASH_SDKENDADDRESS_0X08008FFF     (FLASH_SDKR_SDK_END_4                                                                      | FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X080097FF     (FLASH_SDKR_SDK_END_4                                               | FLASH_SDKR_SDK_END_1                       )
#define LL_FLASH_SDKENDADDRESS_0X08009FFF     (FLASH_SDKR_SDK_END_4                                               | FLASH_SDKR_SDK_END_1 | FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X0800A7FF     (FLASH_SDKR_SDK_END_4                        | FLASH_SDKR_SDK_END_2                                              )
#define LL_FLASH_SDKENDADDRESS_0X0800AFFF     (FLASH_SDKR_SDK_END_4                        | FLASH_SDKR_SDK_END_2                        | FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X0800B7FF     (FLASH_SDKR_SDK_END_4                        | FLASH_SDKR_SDK_END_2 | FLASH_SDKR_SDK_END_1                       )
#define LL_FLASH_SDKENDADDRESS_0X0800BFFF     (FLASH_SDKR_SDK_END_4                        | FLASH_SDKR_SDK_END_2 | FLASH_SDKR_SDK_END_1 | FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X0800C7FF     (FLASH_SDKR_SDK_END_4 | FLASH_SDKR_SDK_END_3                                                                     )
#define LL_FLASH_SDKENDADDRESS_0X0800CFFF     (FLASH_SDKR_SDK_END_4 | FLASH_SDKR_SDK_END_3                                                 FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X0800D7FF     (FLASH_SDKR_SDK_END_4 | FLASH_SDKR_SDK_END_3                        | FLASH_SDKR_SDK_END_1                       )
#define LL_FLASH_SDKENDADDRESS_0X0800DFFF     (FLASH_SDKR_SDK_END_4 | FLASH_SDKR_SDK_END_3                        | FLASH_SDKR_SDK_END_1 | FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X0800E7FF     (FLASH_SDKR_SDK_END_4 | FLASH_SDKR_SDK_END_3 | FLASH_SDKR_SDK_END_2                                              )
#define LL_FLASH_SDKENDADDRESS_0X0800EFFF     (FLASH_SDKR_SDK_END_4 | FLASH_SDKR_SDK_END_3 | FLASH_SDKR_SDK_END_2                        | FLASH_SDKR_SDK_END_0)
#define LL_FLASH_SDKENDADDRESS_0X0800F7FF     (FLASH_SDKR_SDK_END_4 | FLASH_SDKR_SDK_END_3 | FLASH_SDKR_SDK_END_2 | FLASH_SDKR_SDK_END_1                       )
#define LL_FLASH_SDKENDADDRESS_0X0800FFFF     (FLASH_SDKR_SDK_END_4 | FLASH_SDKR_SDK_END_3 | FLASH_SDKR_SDK_END_2 | FLASH_SDKR_SDK_END_1 | FLASH_SDKR_SDK_END_0)

/**
  * @}
  */

/** @defgroup FLASH_LL_EC_BOR_LEVEL BOR Level
  * @{
  */
#define LL_FLASH_BOR_LEV0      (0x00000000)                                                         /*!< BOR Reset threshold levels for 1.7V - 1.8V VDD power supply    */
#define LL_FLASH_BOR_LEV1      (                                              FLASH_SDKR_BOR_LEV_0) /*!< BOR Reset threshold levels for 1.9V - 2.0V VDD power supply    */
#define LL_FLASH_BOR_LEV2      (                       FLASH_SDKR_BOR_LEV_1                       ) /*!< BOR Reset threshold levels for 2.1V - 2.2V VDD power supply    */
#define LL_FLASH_BOR_LEV3      (                       FLASH_SDKR_BOR_LEV_1 | FLASH_SDKR_BOR_LEV_0) /*!< BOR Reset threshold levels for 2.3V - 2.4V VDD power supply    */
#define LL_FLASH_BOR_LEV4      (FLASH_SDKR_BOR_LEV_2                                              ) /*!< BOR Reset threshold levels for 2.5V - 2.6V VDD power supply    */
#define LL_FLASH_BOR_LEV5      (FLASH_SDKR_BOR_LEV_2                        | FLASH_SDKR_BOR_LEV_0) /*!< BOR Reset threshold levels for 2.7V - 2.8V VDD power supply    */
#define LL_FLASH_BOR_LEV6      (FLASH_SDKR_BOR_LEV_2 | FLASH_SDKR_BOR_LEV_1                       ) /*!< BOR Reset threshold levels for 2.9V - 3.0V VDD power supply    */
#define LL_FLASH_BOR_LEV7      (FLASH_SDKR_BOR_LEV_2 | FLASH_SDKR_BOR_LEV_1 | FLASH_SDKR_BOR_LEV_0) /*!< BOR Reset threshold levels for 3.1V - 3.2V VDD power supply   */
/**
  * @}
  */

/** @defgroup FLASH_LL_EC_BOR_ENABLE BOR Enable
  * @{
  */
#define LL_FLASH_BOR_DISABLE   (0x00000000U)                    /*!< BOR Reset Disable    */
#define LL_FLASH_BOR_ENABLE    (FLASH_SDKR_BOR_EN)              /*!< BOR Reset Enable     */
/**
  * @}
  */

/** @defgroup FLASH_LL_EC_KEY Key Value
  * @{
  */
#define LL_FLASH_KEY1                  (FLASH_KEY1)
#define LL_FLASH_KEY2                  (FLASH_KEY2)
/**
  * @}
  */

/** @defgroup FLASH_LL_EC_OPTKEY Optkey Value
  * @{
  */
#define LL_FLASH_OPTKEY1               (FLASH_OPTKEY1)
#define LL_FLASH_OPTKEY2               (FLASH_OPTKEY2)
/**
  * @}
  */

/**
  * @}
  */

#define LL_FLASH_TIMMING_SEQUENCE_CONFIG_4M()            \
                                                        do {                                            \
                                                            FLASH->TS0  = (*(uint32_t *)(0x1FFF3238))&0xFF;           \
                                                            FLASH->TS1  = ((*(uint32_t *)(0x1FFF3238))>>16)&0x1FF;    \
                                                            FLASH->TS3  = ((*(uint32_t *)(0x1FFF3238))>>8)&0xFF;      \
                                                            FLASH->TS2P = (*(uint32_t *)(0x1FFF3240))&0xFF;           \
                                                            FLASH->TPS3 = ((*(uint32_t *)(0x1FFF3240))>>16)&0x7FF;    \
                                                            FLASH->PERTPE = (*(uint32_t *)(0x1FFF3248))&0x1FFFF;      \
                                                            FLASH->SMERTPE = (*(uint32_t *)(0x1FFF3250))&0x1FFFF;     \
                                                            FLASH->PRGTPE = (*(uint32_t *)(0x1FFF3258))&0xFFFF;       \
                                                            FLASH->PRETPE = ((*(uint32_t *)(0x1FFF3258))>>16)&0x3FFF; \
                                                         } while(0U)

#define LL_FLASH_TIMMING_SEQUENCE_CONFIG_8M()            \
                                                        do {                                            \
                                                            FLASH->TS0  = (*(uint32_t *)(0x1FFF3260))&0xFF;           \
                                                            FLASH->TS1  = ((*(uint32_t *)(0x1FFF3260))>>16)&0x1FF;    \
                                                            FLASH->TS3  = ((*(uint32_t *)(0x1FFF3260))>>8)&0xFF;      \
                                                            FLASH->TS2P = (*(uint32_t *)(0x1FFF3268))&0xFF;           \
                                                            FLASH->TPS3 = ((*(uint32_t *)(0x1FFF3268))>>16)&0x7FF;    \
                                                            FLASH->PERTPE = (*(uint32_t *)(0x1FFF3270))&0x1FFFF;      \
                                                            FLASH->SMERTPE = (*(uint32_t *)(0x1FFF3278))&0x1FFFF;     \
                                                            FLASH->PRGTPE = (*(uint32_t *)(0x1FFF3280))&0xFFFF;       \
                                                            FLASH->PRETPE = ((*(uint32_t *)(0x1FFF3280))>>16)&0x3FFF; \
                                                         } while(0U)

#define LL_FLASH_TIMMING_SEQUENCE_CONFIG_16M()           \
                                                        do {                                            \
                                                            FLASH->TS0  = (*(uint32_t *)(0x1FFF3288))&0xFF;           \
                                                            FLASH->TS1  = ((*(uint32_t *)(0x1FFF3288))>>16)&0x1FF;    \
                                                            FLASH->TS3  = ((*(uint32_t *)(0x1FFF3288))>>8)&0xFF;      \
                                                            FLASH->TS2P = (*(uint32_t *)(0x1FFF3290))&0xFF;           \
                                                            FLASH->TPS3 = ((*(uint32_t *)(0x1FFF3290))>>16)&0x7FF;    \
                                                            FLASH->PERTPE = (*(uint32_t *)(0x1FFF3298))&0x1FFFF;      \
                                                            FLASH->SMERTPE = (*(uint32_t *)(0x1FFF32A0))&0x1FFFF;     \
                                                            FLASH->PRGTPE = (*(uint32_t *)(0x1FFF32A8))&0xFFFF;       \
                                                            FLASH->PRETPE = ((*(uint32_t *)(0x1FFF32A8))>>16)&0x3FFF; \
                                                         } while(0U)

#define LL_FLASH_TIMMING_SEQUENCE_CONFIG_22P12M()        \
                                                        do {                                            \
                                                            FLASH->TS0  = (*(uint32_t *)(0x1FFF32B0))&0xFF;           \
                                                            FLASH->TS1  = ((*(uint32_t *)(0x1FFF32B0))>>16)&0x1FF;    \
                                                            FLASH->TS3  = ((*(uint32_t *)(0x1FFF32B0))>>8)&0xFF;      \
                                                            FLASH->TS2P = (*(uint32_t *)(0x1FFF32B8))&0xFF;           \
                                                            FLASH->TPS3 = ((*(uint32_t *)(0x1FFF32B8))>>16)&0x7FF;    \
                                                            FLASH->PERTPE = (*(uint32_t *)(0x1FFF32C0))&0x1FFFF;      \
                                                            FLASH->SMERTPE = (*(uint32_t *)(0x1FFF32C8))&0x1FFFF;     \
                                                            FLASH->PRGTPE = (*(uint32_t *)(0x1FFF32D0))&0xFFFF;       \
                                                            FLASH->PRETPE = ((*(uint32_t *)(0x1FFF32D0))>>16)&0x3FFF; \
                                                         } while(0U)

#define LL_FLASH_TIMMING_SEQUENCE_CONFIG_24M()           \
                                                        do {                                            \
                                                            FLASH->TS0  = (*(uint32_t *)(0x1FFF32D8))&0xFF;           \
                                                            FLASH->TS1  = ((*(uint32_t *)(0x1FFF32D8))>>16)&0x1FF;    \
                                                            FLASH->TS3  = ((*(uint32_t *)(0x1FFF32D8))>>8)&0xFF;      \
                                                            FLASH->TS2P = (*(uint32_t *)(0x1FFF32E0))&0xFF;           \
                                                            FLASH->TPS3 = ((*(uint32_t *)(0x1FFF32E0))>>16)&0x7FF;    \
                                                            FLASH->PERTPE = (*(uint32_t *)(0x1FFF32E8))&0x1FFFF;      \
                                                            FLASH->SMERTPE = (*(uint32_t *)(0x1FFF32F0))&0x1FFFF;     \
                                                            FLASH->PRGTPE = (*(uint32_t *)(0x1FFF32F8))&0xFFFF;       \
                                                            FLASH->PRETPE = ((*(uint32_t *)(0x1FFF32F8))>>16)&0x3FFF; \
                                                         } while(0U)


/* Exported macro ------------------------------------------------------------*/
/** @defgroup FLASH_LL_Exported_Macros FLASH Exported Macros
  * @{
  */

/** @defgroup FLASH_LL_EM_WRITE_READ Common Write and read registers Macros
  * @{
  */

/**
  * @brief  Write a value in FLASH register
  * @param  __INSTANCE__ FLASH Instance
  * @param  __REG__ Register to be written
  * @param  __VALUE__ Value to be written in the register
  * @retval None
  */
#define LL_FLASH_WriteReg(__INSTANCE__, __REG__, __VALUE__) WRITE_REG(__INSTANCE__->__REG__, (__VALUE__))

/**
  * @brief  Read a value in FLASH register
  * @param  __INSTANCE__ FLASH Instance
  * @param  __REG__ Register to be read
  * @retval Register value
  */
#define LL_FLASH_ReadReg(__INSTANCE__, __REG__) READ_REG(__INSTANCE__->__REG__)
/**
  * @}
  */

/**
  * @}
  */

/* Exported functions --------------------------------------------------------*/
/** @defgroup FLASH_LL_Exported_Functions FLASH Exported Functions
  * @{
  */

/** @defgroup FLASH_LL_EF_FLAG_Management Flag Management
  * @{
  */

/**
  * @brief  Indicate the status of End of operation flag.
  * @rmtoll SR          EOP              LL_FLASH_IsActiveFlag_EOP
  * @param  FLASHx FLASH Instance.
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsActiveFlag_EOP(FLASH_TypeDef *FLASHx)
{
  return (READ_BIT(FLASHx->SR, FLASH_SR_EOP) == (FLASH_SR_EOP));
}

/**
  * @brief  Indicate the status of WRPERR flag.
  * @rmtoll SR          WPRERR           LL_FLASH_IsActiveFlag_WRPERR
  * @param  FLASHx FLASH Instance.
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsActiveFlag_WRPERR(FLASH_TypeDef *FLASHx)
{
  return (READ_BIT(FLASHx->SR, FLASH_SR_WRPERR) == (FLASH_SR_WRPERR));
}

/**
  * @brief  Indicate the status of OPTVERR flag.
  * @rmtoll SR          OPTVERR          LL_FLASH_IsActiveFlag_OPTVERR
  * @param  FLASHx FLASH Instance.
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsActiveFlag_OPTVERR(FLASH_TypeDef *FLASHx)
{
  return (READ_BIT(FLASHx->SR, FLASH_SR_OPTVERR) == (FLASH_SR_OPTVERR));
}

/**
  * @brief  Indicate the status of BUSY flag.
  * @rmtoll SR          BSY              LL_FLASH_IsActiveFlag_BUSY
  * @param  FLASHx FLASH Instance.
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsActiveFlag_BUSY(FLASH_TypeDef *FLASHx)
{
  return (READ_BIT(FLASHx->SR, FLASH_SR_BSY) == (FLASH_SR_BSY));
}

/**
  * @brief  Clear End of operation flag.
  * @rmtoll SR          EOP              LL_FLASH_ClearFlag_EOP
  * @param  FLASHx FLASH Instance.
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_ClearFlag_EOP(FLASH_TypeDef *FLASHx)
{
  WRITE_REG(FLASHx->SR, FLASH_SR_EOP);
}

/**
  * @brief  Clear WRPERR flag.
  * @rmtoll SR          WRPERR           LL_FLASH_ClearFlag_WRPERR
  * @param  FLASHx FLASH Instance.
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_ClearFlag_WRPERR(FLASH_TypeDef *FLASHx)
{
  WRITE_REG(FLASHx->SR, FLASH_SR_WRPERR);
}

/**
  * @brief  Clear OPTVERR flag.
  * @rmtoll SR          OPTVERR          LL_FLASH_ClearFlag_OPTVERR
  * @param  FLASHx FLASH Instance.
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_ClearFlag_OPTVERR(FLASH_TypeDef *FLASHx)
{
  WRITE_REG(FLASHx->SR, FLASH_SR_OPTVERR);
}

/**
  * @}
  */

/** @defgroup FLASH_LL_EF_ERASE_Management Erase Management
  * @{
  */

/**
  * @brief  Enable Page Program
  * @rmtoll CR          PG               LL_FLASH_EnablePageProgram
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_EnablePageProgram(FLASH_TypeDef *FLASHx)
{
  SET_BIT(FLASHx->CR, FLASH_CR_PG);
}

/**
  * @brief  Enable Mass Erase
  * @rmtoll CR          MER              LL_FLASH_EnableMassErase
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_EnableMassErase(FLASH_TypeDef *FLASHx)
{
  SET_BIT(FLASHx->CR, FLASH_CR_MER);
}

/**
  * @brief  Enable Page Erase
  * @rmtoll CR          PER              LL_FLASH_EnablePageErase
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_EnablePageErase(FLASH_TypeDef *FLASHx)
{
  SET_BIT(FLASHx->CR, FLASH_CR_PER);
}

/**
  * @brief  Enable Sector Erase
  * @rmtoll CR          SER              LL_FLASH_EnableSectorErase
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_EnableSectorErase(FLASH_TypeDef *FLASHx)
{
  SET_BIT(FLASHx->CR, FLASH_CR_SER);
}

/**
  * @brief  Disable Page Program
  * @rmtoll CR          PG               LL_FLASH_DisablePageProgram
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_DisablePageProgram(FLASH_TypeDef *FLASHx)
{
  CLEAR_BIT(FLASHx->CR, FLASH_CR_PG);
}

/**
  * @brief  Disable Mass Erase
  * @rmtoll CR          MER              LL_FLASH_DisableMassErase
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_DisableMassErase(FLASH_TypeDef *FLASHx)
{
  CLEAR_BIT(FLASHx->CR, FLASH_CR_MER);
}

/**
  * @brief  Disable Page Erase
  * @rmtoll CR          PER              LL_FLASH_DisablePageErase
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_DisablePageErase(FLASH_TypeDef *FLASHx)
{
  CLEAR_BIT(FLASHx->CR, FLASH_CR_PER);
}

/**
  * @brief  Disable Sector Erase
  * @rmtoll CR          SER              LL_FLASH_DisableSectorErase
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_DisableSectorErase(FLASH_TypeDef *FLASHx)
{
  CLEAR_BIT(FLASHx->CR, FLASH_CR_SER);
}

/**
  * @brief  Check if Page Program is enabled
  * @rmtoll CR          PG               LL_FLASH_IsEnabledPageProgram
  * @param  FLASHx FLASH Instance
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsEnabledPageProgram(FLASH_TypeDef *FLASHx)
{
  return ((READ_BIT(FLASHx->CR, FLASH_CR_PG) == (FLASH_CR_PG)) ? 1UL : 0UL);
}

/**
  * @brief  Check if Mass erase is enabled
  * @rmtoll CR          MER              LL_FLASH_IsEnabledMassErase
  * @param  FLASHx FLASH Instance
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsEnabledMassErase(FLASH_TypeDef *FLASHx)
{
  return ((READ_BIT(FLASHx->CR, FLASH_CR_MER) == (FLASH_CR_MER)) ? 1UL : 0UL);
}

/**
  * @brief  Check if Page erase is enabled
  * @rmtoll CR          PER              LL_FLASH_IsEnabledPageErase
  * @param  FLASHx FLASH Instance
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsEnabledPageErase(FLASH_TypeDef *FLASHx)
{
  return ((READ_BIT(FLASHx->CR, FLASH_CR_PER) == (FLASH_CR_PER)) ? 1UL : 0UL);
}

/**
  * @brief  Check if Sector Erase is enabled
  * @rmtoll CR          SER              LL_FLASH_IsEnabledSectorErase
  * @param  FLASHx FLASH Instance
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsEnabledSectorErase(FLASH_TypeDef *FLASHx)
{
  return ((READ_BIT(FLASHx->CR, FLASH_CR_SER) == (FLASH_CR_SER)) ? 1UL : 0UL);
}

/**
  * @brief  Set the Erase Address
  * @rmtoll LL_FLASH_SetEraseAddress
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_SetEraseAddress(FLASH_TypeDef *FLASHx,uint32_t address)
{
  *(__IO uint32_t *)(address) = 0xFFFFFFFF;
}

/**
  * @}
  */

/** @defgroup FLASH_LL_EF_Lock_Management Lock Management
  * @{
  */

/**
  * @brief Unlock the Flash
  * @rmtoll KEYR        KEYR             LL_FLASH_Unlock
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_Unlock(FLASH_TypeDef *FLASHx)
{
  SET_BIT(FLASHx->KEYR, LL_FLASH_KEY1);
  SET_BIT(FLASHx->KEYR, LL_FLASH_KEY2);
}

/**
  * @brief  Unlock the Option
  * @rmtoll OPTKEYR     OPTKEYR          LL_FLASH_OBUnlock
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_OBUnlock(FLASH_TypeDef *FLASHx)
{
  SET_BIT(FLASHx->OPTKEYR, LL_FLASH_OPTKEY1);
  SET_BIT(FLASHx->OPTKEYR, LL_FLASH_OPTKEY2);
}

/**
  * @brief  Lock the Flash
  * @rmtoll CR          LOCK             LL_FLASH_Lock
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_Lock(FLASH_TypeDef *FLASHx)
{
  SET_BIT(FLASHx->CR, FLASH_CR_LOCK);
}

/**
  * @brief  Lock the Option
  * @rmtoll CR          OPTLOCK          LL_FLASH_OBLock
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_OBLock(FLASH_TypeDef *FLASHx)
{
  SET_BIT(FLASHx->CR, FLASH_CR_OPTLOCK);
}

/**
  * @brief  Check if Flash is locked
  * @rmtoll CR          LOCK             LL_FLASH_IsLocked
  * @param  FLASHx FLASH Instance
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsLocked(FLASH_TypeDef *FLASHx)
{
  return ((READ_BIT(FLASHx->CR, FLASH_CR_LOCK) == (FLASH_CR_LOCK)) ? 1UL : 0UL);
}

/**
  * @brief  Check if Option is locked
  * @rmtoll CR          OPTLOCK          LL_FLASH_IsOBLocked
  * @param  FLASHx FLASH Instance
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsOBLocked(FLASH_TypeDef *FLASHx)
{
  return ((READ_BIT(FLASHx->CR, FLASH_CR_OPTLOCK) == (FLASH_CR_OPTLOCK)) ? 1UL : 0UL);
}

/**
  * @}
  */

/** @defgroup FLASH_LL_EF_LAUNCH_Management Launch Management
  * @{
  */

/**
  * @brief  Force the Option byte loading
  * @rmtoll CR          OBL_LAUNCH       LL_FLASH_Launch
  * @param  FLASHx FLASH Instance
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_Launch(FLASH_TypeDef *FLASHx)
{
  SET_BIT(FLASHx->CR, FLASH_CR_OBL_LAUNCH);
}

/**
  * @}
  */

/** @defgroup FLASH_LL_EF_IT_Management Interrupt Management
  * @{
  */

/**
  * @brief  Enable Error interrupts.
  * @rmtoll CR          ERRIR            LL_FLASH_EnableIT_ERR
  * @param  FLASHx FLASH Instance.
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_EnableIT_ERR(FLASH_TypeDef *FLASHx)
{
  SET_BIT(FLASHx->CR, FLASH_CR_ERRIE);
}

/**
  * @brief  Enable End of operation interrupts.
  * @rmtoll CR          EOPIE            LL_FLASH_EnableIT_EOP
  * @param  FLASHx FLASH Instance.
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_EnableIT_EOP(FLASH_TypeDef *FLASHx)
{
  SET_BIT(FLASHx->CR, FLASH_CR_EOPIE);
}

/**
  * @brief  Disable Error interrupts.
  * @rmtoll CR          ERRIE            LL_FLASH_DisableIT_ERR
  * @param  FLASHx FLASH Instance.
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_DisableIT_ERR(FLASH_TypeDef *FLASHx)
{
  CLEAR_BIT(FLASHx->CR, FLASH_CR_ERRIE);
}

/**
  * @brief  Disable End of operation interrupts.
  * @rmtoll CR          EOPIE            LL_FLASH_DisableIT_EOP
  * @param  FLASHx FLASH Instance.
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_DisableIT_EOP(FLASH_TypeDef *FLASHx)
{
  CLEAR_BIT(FLASHx->CR, FLASH_CR_EOPIE);
}

/**
  * @brief  Check if Error interrupts are enabled or disabled.
  * @rmtoll CR          ERRIE            LL_FLASH_IsEnabledIT_ERR
  * @param  FLASHx FLASH Instance.
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsEnabledIT_ERR(FLASH_TypeDef *FLASHx)
{
  return (READ_BIT(FLASH->CR, FLASH_CR_ERRIE) == (FLASH_CR_ERRIE));
}

/**
  * @brief  Check if End of operation interrupts are enabled or disabled.
  * @rmtoll CR          EOPIE            LL_FLASH_IsEnabledIT_EOP
  * @param  FLASHx FLASH Instance.
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsEnabledIT_EOP(FLASH_TypeDef *FLASHx)
{
  return (READ_BIT(FLASH->CR, FLASH_CR_EOPIE) == (FLASH_CR_EOPIE));
}

/**
  * @}
  */

/** @defgroup FLASH_LL_EF_PROGRAM_Management Program Management
  * @{
  */

/**
  * @brief  Enable Page Program Start
  * @rmtoll CR          PGSTRT           LL_FLASH_EnablePageProgramStart
  * @param  FLASHx FLASH Instance
  * @retval None.
  */
__STATIC_INLINE void LL_FLASH_EnablePageProgramStart(FLASH_TypeDef *FLASHx)
{
  SET_BIT(FLASHx->CR, FLASH_CR_PGSTRT);
}

/**
  * @brief  Enable Option Program Start
  * @rmtoll CR          OPTSTRT          LL_FLASH_EnableOptionProgramStart
  * @param  FLASHx FLASH Instance
  * @retval None.
  */
__STATIC_INLINE void LL_FLASH_EnableOptionProgramStart(FLASH_TypeDef *FLASHx)
{
  SET_BIT(FLASHx->CR, FLASH_CR_OPTSTRT);
}

/**
  * @brief  Disable Page Program start
  * @rmtoll CR          PGSTRT           LL_FLASH_DisablePageProgramStart
  * @param  FLASHx FLASH Instance
  * @retval None.
  */
__STATIC_INLINE void LL_FLASH_DisablePageProgramStart(FLASH_TypeDef *FLASHx)
{
  CLEAR_BIT(FLASHx->CR, FLASH_CR_PGSTRT);
}

/**
  * @brief  Disable Option Program start
  * @rmtoll CR          OPTSTRT          LL_FLASH_DisableOptionProgramStart
  * @param  FLASHx FLASH Instance
  * @retval None.
  */
__STATIC_INLINE void LL_FLASH_DisableOptionProgramStart(FLASH_TypeDef *FLASHx)
{
  CLEAR_BIT(FLASHx->CR, FLASH_CR_OPTSTRT);
}

/**
  * @brief  Check if Page Program Start is enabled
  * @rmtoll CR          PGSTRT           LL_FLASH_IsEnabledPageProgramStart
  * @param  FLASHx FLASH Instance
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsEnabledPageProgramStart(FLASH_TypeDef *FLASHx)
{
  return (READ_BIT(FLASHx->CR, FLASH_CR_PGSTRT) == (FLASH_CR_PGSTRT));
}

/**
  * @brief  Check if Option program Start is enabled
  * @rmtoll CR          OPTSTRT         LL_FLASH_IsEnabledOptionProgramStart
  * @param  FLASHx FLASH Instance
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsEnabledOptionProgramStart(FLASH_TypeDef *FLASHx)
{
  return (READ_BIT(FLASHx->CR, FLASH_CR_OPTSTRT) == (FLASH_CR_OPTSTRT));
}

/**
  * @brief  Trigger the Option Program
  * @param  FLASHx FLASH Instance
  * @retval None.
  */
__STATIC_INLINE void LL_FLASH_TriggerOptionProgramStart(FLASH_TypeDef *FLASHx)
{
  *((__IO uint32_t *)(0x40022080))=0xff;
}

/**
  * @brief  Program the page
  * @rmtoll CR          PGSTRT          LL_FLASH_PageProgram
  * @param  FLASHx FLASH Instance
  * @param  Address   Program Address
  * @param  DataAddress Data Address
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_PageProgram(FLASH_TypeDef *FLASHx,uint32_t Address, uint32_t * DataAddress)
{
  uint8_t index=0;
  uint32_t dest = Address;
  uint32_t * src = DataAddress;
  uint32_t primask_bit;
  /* Enter critical section */
  primask_bit = __get_PRIMASK();
  __disable_irq();
  /* 32 words*/
  while(index<64U)
  {
    *(uint32_t *)dest = *src;
    src += 1U;
    dest += 4U;
    index++;
    if(index==63)
    {
      LL_FLASH_EnablePageProgramStart(FLASHx);
    }
  }

  /* Exit critical section: restore previous priority mask */
  __set_PRIMASK(primask_bit);
}

/**
  * @}
  */

/** @defgroup FLASH_LL_EF_OPTR_Management OPTR Management
  * @{
  */

/**
  * @brief  Get nBOOT1 Mode
  * @rmtoll OPTR       nBOOT1           LL_FLASH_GetnBOOT1Mode
  * @param  FLASHx FLASH Instance
  * @retval Returned value can be one of the following values:
  *         @arg @ref LL_FLASH_NBOOT1_CLR
  *         @arg @ref LL_FLASH_NBOOT1_SET
  */
__STATIC_INLINE uint32_t LL_FLASH_GetnBOOT1Mode(FLASH_TypeDef *FLASHx)
{
  return READ_BIT(FLASHx->OPTR, FLASH_OPTR_nBOOT1);
}

/**
  * @brief  Get NRST Mode
  * @rmtoll OPTR        NRST_MODE       LL_FLASH_GetNrstMode
  * @param  FLASHx FLASH Instance
  * @retval Returned value can be one of the following values:
  *         @arg @ref LL_FLASH_NRST_MODE_RESET
  *         @arg @ref LL_FLASH_NRST_MODE_GPIO
  */
__STATIC_INLINE uint32_t LL_FLASH_GetNrstMode(FLASH_TypeDef *FLASHx)
{
  return READ_BIT(FLASHx->OPTR, FLASH_OPTR_NRST_MODE);
}

/**
  * @brief  Get IWDG In Stop Mode
  * @rmtoll OPTR        IWDG_STOP       LL_FLASH_GetIWDGInStop
  * @param  FLASHx FLASH Instance
  * @retval Returned value can be one of the following values:
  *         @arg @ref LL_FLASH_IWDG_INSTOP_FREEZE
  *         @arg @ref LL_FLASH_IWDG_INSTOP_ACTIVE
  */
__STATIC_INLINE uint32_t LL_FLASH_GetIWDGInStop(FLASH_TypeDef *FLASHx)
{
  return READ_BIT(FLASHx->OPTR, FLASH_OPTR_IWDG_STOP);
}

/**
  * @brief  Get WWDG SW Mode
  * @rmtoll OPTR        WWDG_SW       LL_FLASH_GetWWDGSWMode
  * @param  FLASHx FLASH Instance
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_GetWWDGSWMode(FLASH_TypeDef *FLASHx)
{
  return (READ_BIT(FLASHx->OPTR, FLASH_OPTR_WWDG_SW) == (FLASH_OPTR_WWDG_SW));
}

/**
  * @brief  Get IWDG SW Mode
  * @rmtoll OPTR        IWDG_SW          LL_FLASH_GetIWDGSWMode
  * @param  FLASHx FLASH Instance
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_GetIWDGSWMode(FLASH_TypeDef *FLASHx)
{
  return (READ_BIT(FLASHx->OPTR, FLASH_OPTR_IWDG_SW) != (FLASH_OPTR_IWDG_SW));
}

/**
  * @}
  */

/** @defgroup FLASH_LL_EF_SDK_Management SDK Management
  * @{
  */

/**
  * @brief  Set SDK Address
  * @rmtoll SDKR        SDK_STRT        LL_FLASH_SetSDKAddress
  * @rmtoll SDKR        SDK_END         LL_FLASH_SetSDKAddress
  * @rmtoll SDKR        BOR_LEVEL       LL_FLASH_SetSDKAddress
  * @rmtoll SDKR        BOR_EN          LL_FLASH_SetSDKAddress
  * @param  FLASHx FLASH Instance
  * @param  SDKStartAddress This parameter can be one of the following values:
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08000000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08000800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08001000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08001800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08002000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08002800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08003000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08003800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08004000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08004800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08005000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08005800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08006000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08006800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08007000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08007800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08008000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08008800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08009000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08009800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800A000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800A800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800B000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800B800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800C000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800C800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800D000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800D800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800E000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800E800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800F000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800F800
  * @param  SDKEndAddress This parameter can be one of the following values:
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080007FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08000FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080017FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08001FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080027FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08002FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080037FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08003FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080047FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08004FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080057FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08005FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080067FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08006FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080077FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08007FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080087FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08008FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080097FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08009FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800A7FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800AFFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800B7FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800BFFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800C7FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800CFFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800D7FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800DFFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800E7FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800EFFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800F7FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800FFFF
  * @param  BOREnable This parameter can be one of the following values:
  *         @arg @ref LL_FLASH_BOR_ENABLE
  *         @arg @ref LL_FLASH_BOR_DISABLE
  * @param  BORLevel This parameter can be one of the following values:
  *         @arg @ref LL_FLASH_BOR_LEV0
  *         @arg @ref LL_FLASH_BOR_LEV1
  *         @arg @ref LL_FLASH_BOR_LEV2
  *         @arg @ref LL_FLASH_BOR_LEV3
  *         @arg @ref LL_FLASH_BOR_LEV4
  *         @arg @ref LL_FLASH_BOR_LEV5
  *         @arg @ref LL_FLASH_BOR_LEV6
  *         @arg @ref LL_FLASH_BOR_LEV7
  * @retval None.
  */
__STATIC_INLINE void LL_FLASH_SetSDKAddress(FLASH_TypeDef *FLASHx,uint32_t SDKStartAddress,uint32_t SDKEndAddress, \
                                                                  uint32_t BOREnable,      uint32_t BORLevel)
{
  MODIFY_REG(FLASHx->SDKR, (FLASH_SDKR_SDK_STRT | FLASH_SDKR_SDK_END | FLASH_SDKR_BOR_LEV | FLASH_SDKR_BOR_EN) , \
                           (SDKStartAddress | SDKEndAddress | BOREnable | BORLevel));
}

/**
  * @brief  Get SDK Start Address
  * @rmtoll SDKR        SDK_STRT        LL_FLASH_GetSDKStartAddress
  * @param  FLASHx FLASH Instance
  * @retval Returned value can be a one of the following values:
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08000000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08000800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08001000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08001800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08002000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08002800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08003000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08003800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08004000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08004800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08005000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08005800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08006000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08006800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08007000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08007800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08008000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08008800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08009000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X08009800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800A000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800A800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800B000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800B800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800C000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800C800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800D000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800D800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800E000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800E800
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800F000
  *         @arg @ref LL_FLASH_SDKSTARTADDRESS_0X0800F800
  * @retval None
  */
__STATIC_INLINE uint32_t LL_FLASH_GetSDKStartAddress(FLASH_TypeDef *FLASHx)
{
  return READ_BIT(FLASHx->SDKR, FLASH_SDKR_SDK_STRT);
}

/**
  * @brief  Get SDK End Address
  * @rmtoll SDKR        SDK_END         LL_FLASH_GetSDKEndAddress
  * @param  FLASHx FLASH Instance
  * @retval Returned value can be a one of the following values:
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080007FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08000FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080017FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08001FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080027FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08002FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080037FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08003FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080047FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08004FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080057FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08005FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080067FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08006FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080077FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08007FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080087FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08008FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X080097FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X08009FFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800A7FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800AFFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800B7FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800BFFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800C7FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800CFFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800D7FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800DFFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800E7FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800EFFF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800F7FF
  *         @arg @ref LL_FLASH_SDKENDADDRESS_0X0800FFFF
  * @retval None
  */
__STATIC_INLINE uint32_t LL_FLASH_GetSDKEndAddress(FLASH_TypeDef *FLASHx)
{
  return (READ_BIT(FLASHx->SDKR, FLASH_SDKR_SDK_END));
}

/**
  * @brief  Check if BOR is enabled
  * @rmtoll SDKR        BOR_EN          LL_FLASH_IsEnabledBOR
  * @param  FLASHx FLASH Instance
  * @retval State of bit (1 or 0).
  */
__STATIC_INLINE uint32_t LL_FLASH_IsEnabledBOR(FLASH_TypeDef *FLASHx)
{
  return (READ_BIT(FLASHx->SDKR, FLASH_SDKR_BOR_EN) == (FLASH_SDKR_BOR_EN));
}

/**
  * @brief  Get the BOR Level
  * @rmtoll SDKR        BOR_LEV         LL_FLASH_GetBORLevel
  * @param  FLASHx FLASH Instance
  * @retval Returned value can be one of the following values:
  *         @arg @ref LL_FLASH_BOR_LEV0
  *         @arg @ref LL_FLASH_BOR_LEV1
  *         @arg @ref LL_FLASH_BOR_LEV2
  *         @arg @ref LL_FLASH_BOR_LEV3
  *         @arg @ref LL_FLASH_BOR_LEV4
  *         @arg @ref LL_FLASH_BOR_LEV5
  *         @arg @ref LL_FLASH_BOR_LEV6
  *         @arg @ref LL_FLASH_BOR_LEV7
  */
__STATIC_INLINE uint32_t LL_FLASH_GetBORLevel(FLASH_TypeDef *FLASHx)
{
  return READ_BIT(FLASHx->SDKR, FLASH_SDKR_BOR_LEV);
}

/**
  * @}
  */

/** @defgroup FLASH_LL_EF_WRP_Management WRP Management
  * @{
  */
/**
  * @brief  Set WRP Address
  * @note   Depending on devices and packages, some Sectors may not be available.
  *         Refer to device datasheet for Sectors availability.
  * @rmtoll WRPR        WRP             LL_FLASH_SetWRPRAddress
  * @param  FLASHx FLASH Instance
  * @param  Address This parameter can be a combination of the following values:
  *         @arg @ref LL_FLASH_WRP_DISABLE
  *         @arg @ref LL_FLASH_WRP_SECTOR_0
  *         @arg @ref LL_FLASH_WRP_SECTOR_1
  *         @arg @ref LL_FLASH_WRP_SECTOR_2
  *         @arg @ref LL_FLASH_WRP_SECTOR_3
  *         @arg @ref LL_FLASH_WRP_SECTOR_4
  *         @arg @ref LL_FLASH_WRP_SECTOR_5
  *         @arg @ref LL_FLASH_WRP_SECTOR_6
  *         @arg @ref LL_FLASH_WRP_SECTOR_7
  *         @arg @ref LL_FLASH_WRP_SECTOR_8
  *         @arg @ref LL_FLASH_WRP_SECTOR_9
  *         @arg @ref LL_FLASH_WRP_SECTOR_10
  *         @arg @ref LL_FLASH_WRP_SECTOR_11
  *         @arg @ref LL_FLASH_WRP_SECTOR_12
  *         @arg @ref LL_FLASH_WRP_SECTOR_13
  *         @arg @ref LL_FLASH_WRP_SECTOR_14
  *         @arg @ref LL_FLASH_WRP_SECTOR_15
  *         @arg @ref LL_FLASH_WRP_Pages0to31
  *         @arg @ref LL_FLASH_WRP_Pages32to63
  *         @arg @ref LL_FLASH_WRP_Pages64to95
  *         @arg @ref LL_FLASH_WRP_Pages96to127
  *         @arg @ref LL_FLASH_WRP_Pages128to159
  *         @arg @ref LL_FLASH_WRP_Pages160to191
  *         @arg @ref LL_FLASH_WRP_Pages192to223
  *         @arg @ref LL_FLASH_WRP_Pages224to255
  *         @arg @ref LL_FLASH_WRP_Pages256to287
  *         @arg @ref LL_FLASH_WRP_Pages288to319
  *         @arg @ref LL_FLASH_WRP_Pages320to351
  *         @arg @ref LL_FLASH_WRP_Pages352to383
  *         @arg @ref LL_FLASH_WRP_Pages384to415
  *         @arg @ref LL_FLASH_WRP_Pages416to447
  *         @arg @ref LL_FLASH_WRP_Pages448to479
  *         @arg @ref LL_FLASH_WRP_Pages480to511
  *         @arg @ref LL_FLASH_WRP_AllPages
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_SetWRPAddress(FLASH_TypeDef *FLASHx,uint32_t Address)
{
  MODIFY_REG(FLASHx->WRPR, FLASH_WRPR_WRP , ((~(Address)) & FLASH_WRPR_WRP));
}

/**
  * @brief  Get WRP Address
  * @note   Depending on devices and packages, some Sectors may not be available.
  *         Refer to device datasheet for Sectors availability.
  * @rmtoll WRPR        WRP             LL_FLASH_GetWRPAddress
  * @param  FLASHx FLASH Instance
  * @retval Returned value can be a combination of the following values:
  *         @arg @ref LL_FLASH_WRP_DISABLE
  *         @arg @ref LL_FLASH_WRP_SECTOR_0
  *         @arg @ref LL_FLASH_WRP_SECTOR_1
  *         @arg @ref LL_FLASH_WRP_SECTOR_2
  *         @arg @ref LL_FLASH_WRP_SECTOR_3
  *         @arg @ref LL_FLASH_WRP_SECTOR_4
  *         @arg @ref LL_FLASH_WRP_SECTOR_5
  *         @arg @ref LL_FLASH_WRP_SECTOR_6
  *         @arg @ref LL_FLASH_WRP_SECTOR_7
  *         @arg @ref LL_FLASH_WRP_SECTOR_8
  *         @arg @ref LL_FLASH_WRP_SECTOR_9
  *         @arg @ref LL_FLASH_WRP_SECTOR_10
  *         @arg @ref LL_FLASH_WRP_SECTOR_11
  *         @arg @ref LL_FLASH_WRP_SECTOR_12
  *         @arg @ref LL_FLASH_WRP_SECTOR_13
  *         @arg @ref LL_FLASH_WRP_SECTOR_14
  *         @arg @ref LL_FLASH_WRP_SECTOR_15
  *         @arg @ref LL_FLASH_WRP_Pages0to31
  *         @arg @ref LL_FLASH_WRP_Pages32to63
  *         @arg @ref LL_FLASH_WRP_Pages64to95
  *         @arg @ref LL_FLASH_WRP_Pages96to127
  *         @arg @ref LL_FLASH_WRP_Pages128to159
  *         @arg @ref LL_FLASH_WRP_Pages160to191
  *         @arg @ref LL_FLASH_WRP_Pages192to223
  *         @arg @ref LL_FLASH_WRP_Pages224to255
  *         @arg @ref LL_FLASH_WRP_Pages256to287
  *         @arg @ref LL_FLASH_WRP_Pages288to319
  *         @arg @ref LL_FLASH_WRP_Pages320to351
  *         @arg @ref LL_FLASH_WRP_Pages352to383
  *         @arg @ref LL_FLASH_WRP_Pages384to415
  *         @arg @ref LL_FLASH_WRP_Pages416to447
  *         @arg @ref LL_FLASH_WRP_Pages448to479
  *         @arg @ref LL_FLASH_WRP_Pages480to511
  *         @arg @ref LL_FLASH_WRP_AllPages
  */
__STATIC_INLINE uint32_t LL_FLASH_GetWRPAddress(FLASH_TypeDef *FLASHx)
{
  return ((~(READ_BIT(FLASHx->WRPR, FLASH_WRPR_WRP))) & FLASH_WRPR_WRP);
}
/**
  * @}
  */

/** @defgroup FLASH_LL_EF_OPTR_Management OPTR Management
  * @{
  */

/**
  * @brief  Set Optr
  * @rmtoll OPTR            IWDG_STOP        LL_FLASH_SetOPTR
  * @rmtoll OPTR            nBOOT1           LL_FLASH_SetOPTR
  * @rmtoll OPTR            IWDG_SW          LL_FLASH_SetOPTR
  * @rmtoll OPTR            WWDG_SW          LL_FLASH_SetOPTR
  * @rmtoll OPTR            NRST_MODE        LL_FLASH_SetOPTR
  * @rmtoll OPTR            RDP              LL_FLASH_SetOPTR
  * @param  FLASHx FLASH Instance
  * @param  IWDGInStop This parameter can be one of the following values:
  *         @arg @ref LL_FLASH_IWDG_INSTOP_FREEZE
  *         @arg @ref LL_FLASH_IWDG_INSTOP_ACTIVE
  * @param  nBOOT1Mode This parameter can be one of the following values:
  *         @arg @ref LL_FLASH_NBOOT1_CLR
  *         @arg @ref LL_FLASH_NBOOT1_SET
  * @param  IWDGMode This parameter can be one of the following values:
  *         @arg @ref LL_FLASH_IWDG_MODE_HW
  *         @arg @ref LL_FLASH_IWDG_MODE_SW
  * @param  WWDGMode This parameter can be one of the following values:
  *         @arg @ref LL_FLASH_WWDG_MODE_HW
  *         @arg @ref LL_FLASH_WWDG_MODE_SW
  * @param  NrstMode This parameter can be one of the following values:
  *         @arg @ref LL_FLASH_NRST_MODE_RESET
  *         @arg @ref LL_FLASH_NRST_MODE_GPIO
  * @param  RDPLevel This parameter can be one of the following values:
  *         @arg @ref LL_FLASH_RDP_LEVEL_0
  *         @arg @ref LL_FLASH_RDP_LEVEL_1
  * @retval None
  */
__STATIC_INLINE void LL_FLASH_SetOPTR(FLASH_TypeDef *FLASHx, \
                                      uint32_t IWDGInStop, uint32_t nBOOT1Mode, uint32_t IWDGMode, \
                                      uint32_t WWDGMode,   uint32_t NrstMode,   uint32_t RDPLevel)
{
  MODIFY_REG(FLASHx->OPTR, (FLASH_OPTR_IWDG_STOP | FLASH_OPTR_nBOOT1    | FLASH_OPTR_IWDG_SW | \
                            FLASH_OPTR_WWDG_SW   | FLASH_OPTR_NRST_MODE | FLASH_OPTR_RDP),     \
                           (IWDGInStop | nBOOT1Mode | WWDGMode | IWDGMode | NrstMode | RDPLevel));
}

/**
  * @brief  Get RDP Level
  * @rmtoll OPTR        RDP             LL_FLASH_GetRDP
  * @param  FLASHx FLASH Instance
  * @retval Returned value can be a combination of the following values:
  *         @arg @ref LL_FLASH_RDP_LEVEL_0
  *         @arg @ref LL_FLASH_RDP_LEVEL_1
  */
__STATIC_INLINE uint32_t LL_FLASH_GetRDP(FLASH_TypeDef *FLASHx)
{
  return (READ_BIT(FLASHx->OPTR, FLASH_OPTR_RDP));
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
#endif /* defined (FLASH) */

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* PY32F071_LL_FLASH_H */

/************************ (C) COPYRIGHT Puya *****END OF FILE****/
