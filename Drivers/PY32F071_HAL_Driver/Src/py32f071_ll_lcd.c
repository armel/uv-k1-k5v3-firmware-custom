/**
  ******************************************************************************
  * @file    py32f071_ll_lcd.c
  * @author  MCU Application Team
  * @brief   LCD LL module driver.
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
#if defined(USE_FULL_LL_DRIVER)

/* Includes ------------------------------------------------------------------*/
#include "py32f071_ll_lcd.h"
#include "py32f071_ll_bus.h"
#ifdef  USE_FULL_ASSERT
  #include "py32_assert.h"
#else
  #define assert_param(expr) ((void)0U)
#endif

/** @addtogroup PY32F071_LL_Driver
  * @{
  */

#if defined (LCD)
/** @addtogroup LCD_LL
  * @{
  */

/* Private types -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private constants ---------------------------------------------------------*/
/* Private macros ------------------------------------------------------------*/

/** @addtogroup LCD_LL_Private_Macros LCD Private Macros
  * @{
  */
#define IS_LCD_CONTRAST(__CONTRAST__) (((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_0) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_1) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_2) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_3) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_4) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_5) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_6) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_7) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_8) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_9) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_10) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_11) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_12) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_13) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_14) || \
                                       ((__CONTRAST__) == LL_LCD_CONTRASTLEVEL_15))

#define IS_LCD_BIAS_SRC(__SOURCE__)   (((__SOURCE__) == LL_LCD_BIAS_SRC_IN_RES_HIGH_POWER) || \
                                       ((__SOURCE__) == LL_LCD_BIAS_SRC_IN_RES_LOW_POWER) || \
                                       ((__SOURCE__) == LL_LCD_BIAS_SRC_IN_RES_MID_POWER) || \
                                       ((__SOURCE__) == LL_LCD_BIAS_SRC_EXT_RES))

#define IS_LCD_DUTY(__DUTY__)         (((__DUTY__) == LL_LCD_DUTY_STATIC)  || \
                                       ((__DUTY__) == LL_LCD_DUTY_1_2)     || \
                                       ((__DUTY__) == LL_LCD_DUTY_1_3)     || \
                                       ((__DUTY__) == LL_LCD_DUTY_1_4)     || \
                                       ((__DUTY__) == LL_LCD_DUTY_1_6)     || \
                                       ((__DUTY__) == LL_LCD_DUTY_1_8))

#define IS_LCD_BIAS(__BIAS__)         (((__BIAS__) == LL_LCD_BIAS_1_3) || \
                                       ((__BIAS__) == LL_LCD_BIAS_1_2))

#define IS_LCD_SCAN_FRE(__FRE__)      (((__FRE__) == LL_LCD_SCAN_FRE_64HZ) || \
                                       ((__FRE__) == LL_LCD_SCAN_FRE_128HZ) || \
                                       ((__FRE__) == LL_LCD_SCAN_FRE_256HZ) || \
                                       ((__FRE__) == LL_LCD_SCAN_FRE_512HZ))

#define IS_LCD_MODE(__MODE__)         (((__MODE__) == LL_LCD_MODE_0) || \
                                       ((__MODE__) == LL_LCD_MODE_1))
/**
  * @}
  */

/* Private function prototypes -----------------------------------------------*/
/* Exported functions --------------------------------------------------------*/
/** @addtogroup LCD_LL_Exported_Functions LCD Exported Functions
  * @{
  */

/** @addtogroup LCD_LL_EF_Init Initialization and de-initialization functions
  * @{
  */

/**
  * @brief  De-initialize LCD registers (Registers restored to their default values).
  * @param  LCDx LCD Instance
  * @retval An ErrorStatus enumeration value:
  *          - SUCCESS: LCD registers are de-initialized
  *          - ERROR:   Wrong LCD Instance
  */
ErrorStatus LL_LCD_DeInit(LCD_TypeDef *LCDx)
{
  ErrorStatus status = SUCCESS;

  /* Check the parameters */
  assert_param(IS_LCD_ALL_INSTANCE(LCDx));

  /* Force and Release reset on clock of LCDx */
  if (LCDx == LCD)
  {
    LL_APB1_GRP2_ForceReset(LL_APB1_GRP2_PERIPH_LCD);
    LL_APB1_GRP2_ReleaseReset(LL_APB1_GRP2_PERIPH_LCD);
  }
  else
  {
    status = ERROR;
  }

  return (status);
}

/**
  * @brief  Initializes the LCD peripheral according to the specified parameters 
  *         in the LCD_InitStruct.
  * @param  LCDx LCD Instance
  * @param  LCD_InitStruct pointer to a @ref LL_LCD_InitTypeDef structure
  *         that contains the configuration information for the specified LCD peripheral.
  * @retval An ErrorStatus enumeration value:
  *          - SUCCESS: LCD registers are initialized according to LCD_InitStruct content
  *          - ERROR:   Not applicable
  */
ErrorStatus LL_LCD_Init(LCD_TypeDef *LCDx, LL_LCD_InitTypeDef *LCD_InitStruct)
{
  /* Check function parameters */
  assert_param(IS_LCD_ALL_INSTANCE(LCDx));
  assert_param(IS_LCD_CONTRAST(LCD_InitStruct->Contrast));
  assert_param(IS_LCD_BIAS_SRC(LCD_InitStruct->BiasSrc));
  assert_param(IS_LCD_DUTY(LCD_InitStruct->Duty));
  assert_param(IS_LCD_BIAS(LCD_InitStruct->Bias));
  assert_param(IS_LCD_SCAN_FRE(LCD_InitStruct->ScanFre));
  assert_param(IS_LCD_MODE(LCD_InitStruct->Mode));
  
  /* Disable the peripheral */
  LL_LCD_Disable(LCD);
  
  /* Configure LCD Contrast, Bias Source, Duty, Bias, Scan Frequency */
  MODIFY_REG(LCDx->CR0, \
             (LCD_CR0_CONTRAST | LCD_CR0_BSEL | LCD_CR0_DUTY | LCD_CR0_BIAS | LCD_CR0_LCDCLK), \
             (LCD_InitStruct->Contrast | LCD_InitStruct->BiasSrc | LCD_InitStruct->Duty | LCD_InitStruct->Bias | LCD_InitStruct->ScanFre));
  
  /* Configure LCD Mode */
  MODIFY_REG(LCDx->CR1, LCD_CR1_MODE, LCD_InitStruct->Mode);
  
  /* Enable the peripheral */
  LL_LCD_Enable(LCD);

  return (SUCCESS);
}

/**
  * @brief Set each @ref LL_LCD_InitTypeDef field to default value.
  * @param LCD_InitStruct pointer to a @ref LL_LCD_InitTypeDef structure
  *        whose fields will be set to default values.
  * @retval None
  */
void LL_LCD_StructInit(LL_LCD_InitTypeDef *LCD_InitStruct)
{
  /* Reset LCD init structure parameters values */
  LCD_InitStruct->Contrast = LL_LCD_CONTRASTLEVEL_0;
  LCD_InitStruct->BiasSrc  = LL_LCD_BIAS_SRC_EXT_RES;
  LCD_InitStruct->Duty     = LL_LCD_DUTY_1_4;
  LCD_InitStruct->Bias     = LL_LCD_BIAS_1_3;
  LCD_InitStruct->ScanFre  = LL_LCD_SCAN_FRE_128HZ;
  LCD_InitStruct->Mode     = LL_LCD_MODE_0;
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
#endif /* defined (LCD) */

/**
  * @}
  */
#endif /* USE_FULL_LL_DRIVER */

/************************ (C) COPYRIGHT Puya Semiconductor *****END OF FILE****/
