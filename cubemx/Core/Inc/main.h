/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

#include "stm32h7xx_ll_tim.h"
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_cortex.h"
#include "stm32h7xx_ll_rcc.h"
#include "stm32h7xx_ll_system.h"
#include "stm32h7xx_ll_utils.h"
#include "stm32h7xx_ll_pwr.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_dma.h"
#include "stm32h7xx_ll_hsem.h"

#include "stm32h7xx_ll_exti.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define TFT_RST_Pin GPIO_PIN_3
#define TFT_RST_GPIO_Port GPIOE
#define SD_CS_Pin GPIO_PIN_4
#define SD_CS_GPIO_Port GPIOE
#define LED_R_Pin GPIO_PIN_5
#define LED_R_GPIO_Port GPIOE
#define LED_G_Pin GPIO_PIN_6
#define LED_G_GPIO_Port GPIOE
#define BIO5_Pin GPIO_PIN_2
#define BIO5_GPIO_Port GPIOF
#define IN_R_Pin GPIO_PIN_0
#define IN_R_GPIO_Port GPIOA
#define LDAC_CTRL_Pin GPIO_PIN_3
#define LDAC_CTRL_GPIO_Port GPIOA
#define VIN_SENSE_Pin GPIO_PIN_11
#define VIN_SENSE_GPIO_Port GPIOF
#define LED_B_Pin GPIO_PIN_7
#define LED_B_GPIO_Port GPIOE
#define LED_ON_Pin GPIO_PIN_8
#define LED_ON_GPIO_Port GPIOE
#define LED_RX_Pin GPIO_PIN_10
#define LED_RX_GPIO_Port GPIOE
#define LED_TX_Pin GPIO_PIN_12
#define LED_TX_GPIO_Port GPIOE
#define LED_L_Pin GPIO_PIN_15
#define LED_L_GPIO_Port GPIOE
#define SPI_CS_Pin GPIO_PIN_12
#define SPI_CS_GPIO_Port GPIOB
#define BRDSEL_Pin GPIO_PIN_10
#define BRDSEL_GPIO_Port GPIOD
#define AUX_TRGOUT_Pin GPIO_PIN_15
#define AUX_TRGOUT_GPIO_Port GPIOD
#define ADDR0_Pin GPIO_PIN_2
#define ADDR0_GPIO_Port GPIOG
#define ADDR1_Pin GPIO_PIN_3
#define ADDR1_GPIO_Port GPIOG
#define ADDR2_Pin GPIO_PIN_4
#define ADDR2_GPIO_Port GPIOG
#define OE_DO_AH_Pin GPIO_PIN_5
#define OE_DO_AH_GPIO_Port GPIOG
#define OE_DO_IP_Pin GPIO_PIN_6
#define OE_DO_IP_GPIO_Port GPIOG
#define OE_DI_Pin GPIO_PIN_7
#define OE_DI_GPIO_Port GPIOG
#define IN_S_Pin GPIO_PIN_8
#define IN_S_GPIO_Port GPIOG
#define TRG_OUT_Pin GPIO_PIN_6
#define TRG_OUT_GPIO_Port GPIOC
#define ENC_SW_Pin GPIO_PIN_1
#define ENC_SW_GPIO_Port GPIOD
#define BIO1_Pin GPIO_PIN_4
#define BIO1_GPIO_Port GPIOD
#define BIO2_Pin GPIO_PIN_5
#define BIO2_GPIO_Port GPIOD
#define BIO3_Pin GPIO_PIN_6
#define BIO3_GPIO_Port GPIOD
#define BIO4_Pin GPIO_PIN_7
#define BIO4_GPIO_Port GPIOD
#define IN_T_Pin GPIO_PIN_9
#define IN_T_GPIO_Port GPIOG
#define IN_U_Pin GPIO_PIN_10
#define IN_U_GPIO_Port GPIOG
#define IN_V_Pin GPIO_PIN_11
#define IN_V_GPIO_Port GPIOG
#define IN_W_Pin GPIO_PIN_12
#define IN_W_GPIO_Port GPIOG
#define IN_X_Pin GPIO_PIN_13
#define IN_X_GPIO_Port GPIOG
#define BIO6_Pin GPIO_PIN_14
#define BIO6_GPIO_Port GPIOG
#define BIO7_Pin GPIO_PIN_15
#define BIO7_GPIO_Port GPIOG
#define IN_Q_Pin GPIO_PIN_3
#define IN_Q_GPIO_Port GPIOB
#define TFT_CS_Pin GPIO_PIN_0
#define TFT_CS_GPIO_Port GPIOE
#define TFT_DC_Pin GPIO_PIN_1
#define TFT_DC_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
