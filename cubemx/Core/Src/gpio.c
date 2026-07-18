/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins
     PC14-OSC32_IN (OSC32_IN)   ------> RCC_OSC32_IN
     PC15-OSC32_OUT (OSC32_OUT)   ------> RCC_OSC32_OUT
     PH0-OSC_IN (PH0)   ------> RCC_OSC_IN
     PH1-OSC_OUT (PH1)   ------> RCC_OSC_OUT
     PA13 (JTMS/SWDIO)   ------> DEBUG_JTMS-SWDIO
     PA14 (JTCK/SWCLK)   ------> DEBUG_JTCK-SWCLK
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, TFT_RST_Pin|SD_CS_Pin|LED_R_Pin|LED_G_Pin
                          |LED_B_Pin|LED_ON_Pin|LED_RX_Pin|LED_TX_Pin
                          |LED_L_Pin|TFT_CS_Pin|TFT_DC_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LDAC_CTRL_GPIO_Port, LDAC_CTRL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, BRDSEL_Pin|AUX_TRGOUT_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, ADDR0_Pin|ADDR1_Pin|ADDR2_Pin|OE_DO_AH_Pin
                          |OE_DO_IP_Pin|OE_DI_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : TFT_RST_Pin SD_CS_Pin LED_R_Pin LED_G_Pin
                           LED_B_Pin LED_ON_Pin LED_RX_Pin LED_TX_Pin
                           LED_L_Pin TFT_CS_Pin TFT_DC_Pin */
  GPIO_InitStruct.Pin = TFT_RST_Pin|SD_CS_Pin|LED_R_Pin|LED_G_Pin
                          |LED_B_Pin|LED_ON_Pin|LED_RX_Pin|LED_TX_Pin
                          |LED_L_Pin|TFT_CS_Pin|TFT_DC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : BIO5_Pin */
  GPIO_InitStruct.Pin = BIO5_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BIO5_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LDAC_CTRL_Pin */
  GPIO_InitStruct.Pin = LDAC_CTRL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LDAC_CTRL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI_CS_Pin */
  GPIO_InitStruct.Pin = SPI_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SPI_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BRDSEL_Pin AUX_TRGOUT_Pin */
  GPIO_InitStruct.Pin = BRDSEL_Pin|AUX_TRGOUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : ADDR0_Pin ADDR1_Pin ADDR2_Pin OE_DO_AH_Pin
                           OE_DO_IP_Pin OE_DI_Pin */
  GPIO_InitStruct.Pin = ADDR0_Pin|ADDR1_Pin|ADDR2_Pin|OE_DO_AH_Pin
                          |OE_DO_IP_Pin|OE_DI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pins : IN_S_Pin IN_T_Pin IN_U_Pin IN_V_Pin
                           IN_W_Pin IN_X_Pin BIO6_Pin BIO7_Pin */
  GPIO_InitStruct.Pin = IN_S_Pin|IN_T_Pin|IN_U_Pin|IN_V_Pin
                          |IN_W_Pin|IN_X_Pin|BIO6_Pin|BIO7_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pin : ENC_SW_Pin */
  GPIO_InitStruct.Pin = ENC_SW_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ENC_SW_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BIO1_Pin BIO2_Pin BIO3_Pin BIO4_Pin */
  GPIO_InitStruct.Pin = BIO1_Pin|BIO2_Pin|BIO3_Pin|BIO4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
