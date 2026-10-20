/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "adc.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define GAIN_CAN_STD_ID          0x321U
#define GAIN_CAN_TARGET_ADDR     0x20U
#define GAIN_CAN_CMD_DOWN        0xA1U
#define GAIN_CAN_CMD_UP          0xA2U
#define GAIN_CAN_CMD_SET         0xA3U
#define GAIN_LEVEL_MIN           1U
#define GAIN_LEVEL_MAX           4U
#define GAIN_INITIAL_LEVEL       4U

#define SIM_CAN_GPIO_RX_TEST     1U
#define SIM_CAN_SYNC             0xA5U
#define SIM_CAN_BIT_US           50U
#define SIM_CAN_IDLE_READY_MS    20U
#define SIM_CAN_FRAME_BITS       48U
#define SIM_CAN_FIRST_SAMPLE_US  ((SIM_CAN_BIT_US * 2U) + (SIM_CAN_BIT_US / 2U))
#define SIM_CAN_RX_GPIO_PORT     GPIOA
#define SIM_CAN_RX_GPIO_PIN      GPIO_PIN_11

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE BEGIN PV */

static uint8_t current_gain_level = GAIN_INITIAL_LEVEL;
static volatile uint32_t sim_dbg_rx_frame_count = 0U;
static volatile uint32_t sim_dbg_rx_ok_count = 0U;
static volatile uint32_t sim_dbg_rx_bad_count = 0U;
static volatile uint32_t sim_dbg_no_idle_high_count = 0U;
static volatile uint32_t sim_dbg_no_start_low_count = 0U;
static volatile uint32_t sim_dbg_low_timeout_count = 0U;
static volatile uint32_t sim_dbg_low_short_count = 0U;
static volatile uint32_t sim_dbg_high_timeout_count = 0U;
static volatile uint32_t sim_dbg_last_low_us = 0U;
static volatile uint8_t sim_dbg_last_frame[6] = {0};
static volatile uint8_t sim_dbg_last_crc = 0U;
static volatile uint8_t sim_dbg_last_level = 0U;
static volatile uint8_t sim_dbg_last_b0 = 0U;
static volatile uint8_t sim_dbg_last_b1 = 0U;
static volatile uint8_t sim_dbg_last_b2 = 0U;
static volatile uint8_t sim_dbg_last_b3 = 0U;
static volatile uint8_t sim_dbg_last_b4 = 0U;
static volatile uint8_t sim_dbg_last_b5 = 0U;
static volatile uint32_t sim_dbg_fail_mask = 0U;
static volatile uint32_t sim_dbg_est_bit_us = 0U;
static volatile uint32_t sim_dbg_tim_start_count = 0U;
static volatile uint32_t sim_dbg_tim_done_count = 0U;
static volatile uint32_t sim_dbg_tim_overrun_count = 0U;
static volatile uint8_t sim_rx_active = 0U;
static volatile uint8_t sim_rx_frame_ready = 0U;
static volatile uint32_t sim_rx_bit_index = 0U;
static volatile uint8_t sim_rx_frame_buf[6] = {0};
static volatile uint8_t sim_rx_work_buf[6] = {0};

/* USER CODE END PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

#if !SIM_CAN_GPIO_RX_TEST
static void CAN_GainCtrl_Init(void);
static void CAN_GainCtrl_ReceiveProcess(void);
static void CAN_GainCtrl_ParseFrame(const CAN_RxHeaderTypeDef *rx_header, uint8_t *rx_data);
static void CAN_GainCtrl_PrintFrame(const CAN_RxHeaderTypeDef *rx_header, const uint8_t *rx_data);
static void CAN_Debug_PrintStatus(void);
#endif
static void SimCan_GainCtrl_Init(void);
static void SimCan_GainCtrl_ReceiveProcess(void);
static void Gain_SetLevel(uint8_t level);
int __io_putchar(int ch);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  printf("\r\n");
  printf("====================================\r\n");
  printf("STM32 Gain Receiver Start\r\n");
  printf("addr=0x%02X init_level=%u\r\n",
         (unsigned)GAIN_CAN_TARGET_ADDR,
         (unsigned)GAIN_INITIAL_LEVEL);
  printf("====================================\r\n");

  Gain_SetLevel(GAIN_INITIAL_LEVEL);
#if SIM_CAN_GPIO_RX_TEST
  SimCan_GainCtrl_Init();
#else
  CAN_GainCtrl_Init();
#endif

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

#if !SIM_CAN_GPIO_RX_TEST
  uint32_t last_hello_tick = HAL_GetTick();
  CAN_Debug_PrintStatus();
#endif

  while (1)
  {
#if SIM_CAN_GPIO_RX_TEST
    SimCan_GainCtrl_ReceiveProcess();
#else
    CAN_GainCtrl_ReceiveProcess();

    if ((HAL_GetTick() - last_hello_tick) >= 1000U)
    {
      last_hello_tick = HAL_GetTick();
      CAN_Debug_PrintStatus();
    }

    HAL_Delay(5U);
#endif

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }

  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
static uint32_t SimCan_ElapsedUs(uint32_t start_cycles)
{
  uint32_t ticks = (uint32_t)(((uint64_t)SystemCoreClock) / 1000000ULL);
  if (ticks == 0U)
  {
    return 0U;
  }
  return (uint32_t)((uint32_t)(DWT->CYCCNT - start_cycles) / ticks);
}

static uint8_t SimCan_ReadLine(void)
{
  return (HAL_GPIO_ReadPin(SIM_CAN_RX_GPIO_PORT, SIM_CAN_RX_GPIO_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

static void SimCan_Tim2Stop(void)
{
  TIM2->CR1 &= ~TIM_CR1_CEN;
  TIM2->DIER &= ~TIM_DIER_UIE;
  TIM2->SR = 0U;
}

static void SimCan_Tim2Init(void)
{
  __HAL_RCC_TIM2_CLK_ENABLE();
  TIM2->CR1 = 0U;
  TIM2->PSC = (uint16_t)((SystemCoreClock / 1000000U) - 1U);
  TIM2->ARR = SIM_CAN_BIT_US - 1U;
  TIM2->CNT = 0U;
  TIM2->EGR = TIM_EGR_UG;
  TIM2->SR = 0U;
  NVIC_SetPriority(TIM2_IRQn, 1U);
  NVIC_EnableIRQ(TIM2_IRQn);
}

static void SimCan_Tim2StartRx(void)
{
  if (sim_rx_active != 0U)
  {
    sim_dbg_tim_overrun_count++;
    return;
  }

  for (uint32_t i = 0U; i < 6U; i++)
  {
    sim_rx_work_buf[i] = 0U;
  }

  sim_rx_bit_index = 0U;
  sim_rx_active = 1U;
  sim_rx_frame_ready = 0U;
  sim_dbg_est_bit_us = SIM_CAN_BIT_US;
  sim_dbg_tim_start_count++;

  TIM2->CR1 &= ~TIM_CR1_CEN;
  TIM2->PSC = (uint16_t)((SystemCoreClock / 1000000U) - 1U);
  TIM2->ARR = SIM_CAN_FIRST_SAMPLE_US - 1U;
  TIM2->CNT = 0U;
  TIM2->SR = 0U;
  TIM2->DIER |= TIM_DIER_UIE;
  TIM2->CR1 |= TIM_CR1_CEN;
}

void TIM2_IRQHandler(void)
{
  if ((TIM2->SR & TIM_SR_UIF) == 0U)
  {
    return;
  }

  TIM2->SR &= ~TIM_SR_UIF;

  if (sim_rx_active == 0U)
  {
    SimCan_Tim2Stop();
    return;
  }

  uint32_t bit_index = sim_rx_bit_index;
  if (bit_index < SIM_CAN_FRAME_BITS)
  {
    if (SimCan_ReadLine() != 0U)
    {
      sim_rx_work_buf[bit_index >> 3U] |= (uint8_t)(1U << (bit_index & 7U));
    }

    sim_rx_bit_index = bit_index + 1U;
    TIM2->ARR = SIM_CAN_BIT_US - 1U;
  }

  if (sim_rx_bit_index >= SIM_CAN_FRAME_BITS)
  {
    for (uint32_t i = 0U; i < 6U; i++)
    {
      sim_rx_frame_buf[i] = sim_rx_work_buf[i];
    }
    sim_rx_active = 0U;
    sim_rx_frame_ready = 1U;
    sim_dbg_tim_done_count++;
    SimCan_Tim2Stop();
  }
}

static uint8_t SimCan_Crc8(const uint8_t *data, uint32_t len)
{
  uint8_t crc = 0U;

  for (uint32_t i = 0U; i < len; i++)
  {
    crc ^= data[i];
    for (uint32_t bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 0x80U) != 0U)
      {
        crc = (uint8_t)((crc << 1U) ^ 0x07U);
      }
      else
      {
        crc = (uint8_t)(crc << 1U);
      }
    }
  }

  return crc;
}

static uint8_t SimCan_ReadFrame(uint8_t frame[6])
{
  uint32_t low_start_cycles;
  uint32_t low_us;
  static uint32_t idle_high_start_ms = 0U;
  static uint8_t idle_high_ready = 0U;

  if (sim_rx_frame_ready != 0U)
  {
    __disable_irq();
    for (uint32_t i = 0U; i < 6U; i++)
    {
      frame[i] = sim_rx_frame_buf[i];
    }
    sim_rx_frame_ready = 0U;
    __enable_irq();
    return 1U;
  }

  if (sim_rx_active != 0U)
  {
    return 0U;
  }

  if (SimCan_ReadLine() != 0U)
  {
    if (idle_high_ready == 0U)
    {
      if (idle_high_start_ms == 0U)
      {
        idle_high_start_ms = HAL_GetTick();
      }
      else if ((HAL_GetTick() - idle_high_start_ms) >= SIM_CAN_IDLE_READY_MS)
      {
        idle_high_ready = 1U;
      }
    }
    sim_dbg_no_start_low_count++;
    return 0U;
  }

  if (idle_high_ready == 0U)
  {
    idle_high_start_ms = 0U;
    sim_dbg_no_idle_high_count++;
    return 0U;
  }
  idle_high_ready = 0U;
  idle_high_start_ms = 0U;

  low_start_cycles = DWT->CYCCNT;
  while (SimCan_ReadLine() == 0U)
  {
    if (SimCan_ElapsedUs(low_start_cycles) > 300U)
    {
      sim_dbg_low_timeout_count++;
      return 0U;
    }
  }

  low_us = SimCan_ElapsedUs(low_start_cycles);
  sim_dbg_last_low_us = low_us;
  if (low_us < (SIM_CAN_BIT_US / 2U))
  {
    sim_dbg_low_short_count++;
    return 0U;
  }

  sim_dbg_est_bit_us = SIM_CAN_BIT_US;
  SimCan_Tim2StartRx();
  return 0U;
}

static void SimCan_GainCtrl_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  SimCan_Tim2Init();

  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitStruct.Pin = SIM_CAN_RX_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SIM_CAN_RX_GPIO_PORT, &GPIO_InitStruct);

  printf("SIM CAN GPIO receiver ready: rx=PA11 bit=%uus current_level=%u\r\n",
         (unsigned)SIM_CAN_BIT_US,
         (unsigned)current_gain_level);
}

static void SimCan_GainCtrl_ReceiveProcess(void)
{
  uint8_t frame[6] = {0};
  uint8_t crc;
  static uint32_t last_bad_print_tick = 0U;

  if (SimCan_ReadFrame(frame) == 0U)
  {
    return;
  }

  crc = SimCan_Crc8(frame, 5U);
  sim_dbg_rx_frame_count++;
  memcpy((void *)sim_dbg_last_frame, frame, 6U);
  sim_dbg_last_crc = crc;
  sim_dbg_last_b0 = frame[0];
  sim_dbg_last_b1 = frame[1];
  sim_dbg_last_b2 = frame[2];
  sim_dbg_last_b3 = frame[3];
  sim_dbg_last_b4 = frame[4];
  sim_dbg_last_b5 = frame[5];
  sim_dbg_fail_mask = 0U;
  if (frame[0] != SIM_CAN_SYNC) {
    sim_dbg_fail_mask |= 0x01U;
  }
  if (frame[1] != GAIN_CAN_TARGET_ADDR) {
    sim_dbg_fail_mask |= 0x02U;
  }
  if (frame[2] != GAIN_CAN_CMD_SET) {
    sim_dbg_fail_mask |= 0x04U;
  }
  if ((frame[3] < GAIN_LEVEL_MIN) || (frame[3] > GAIN_LEVEL_MAX)) {
    sim_dbg_fail_mask |= 0x08U;
  }
  if (frame[4] != (uint8_t)~frame[3]) {
    sim_dbg_fail_mask |= 0x10U;
  }
  if (frame[5] != crc) {
    sim_dbg_fail_mask |= 0x20U;
  }
  if ((frame[0] != SIM_CAN_SYNC) ||
      (frame[1] != GAIN_CAN_TARGET_ADDR) ||
      (frame[2] != GAIN_CAN_CMD_SET) ||
      (frame[3] < GAIN_LEVEL_MIN) ||
      (frame[3] > GAIN_LEVEL_MAX) ||
      (frame[4] != (uint8_t)~frame[3]) ||
      (frame[5] != crc))
  {
    uint32_t now = HAL_GetTick();
    sim_dbg_rx_bad_count++;
    if ((now - last_bad_print_tick) >= 1000U)
    {
      last_bad_print_tick = now;
      printf("SIM RX bad frame=%02X %02X %02X %02X %02X %02X crc=%02X\r\n",
             frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], crc);
    }
    return;
  }

  sim_dbg_rx_ok_count++;
  sim_dbg_last_level = frame[3];
  Gain_SetLevel(frame[3]);
  printf("SIM RX ok level=%u frame=%02X %02X %02X %02X %02X %02X\r\n",
         (unsigned)current_gain_level,
         frame[0], frame[1], frame[2], frame[3], frame[4], frame[5]);
}

#if !SIM_CAN_GPIO_RX_TEST
static void CAN_Debug_PrintStatus(void)
{
  uint32_t rf0r = hcan.Instance->RF0R;
  uint32_t esr = hcan.Instance->ESR;

  printf("CAN RF0R=0x%08lX FMP0=%lu ESR=0x%08lX TEC=%lu REC=%lu LEC=%lu ERR=0x%08lX\r\n",
         (unsigned long)rf0r,
         (unsigned long)(rf0r & 0x3U),
         (unsigned long)esr,
         (unsigned long)((esr >> 16U) & 0xFFU),
         (unsigned long)((esr >> 24U) & 0xFFU),
         (unsigned long)((esr >> 4U) & 0x7U),
         (unsigned long)HAL_CAN_GetError(&hcan));
}
#endif

int __io_putchar(int ch)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1U, HAL_MAX_DELAY);
  return ch;
}

#if !SIM_CAN_GPIO_RX_TEST
static void CAN_GainCtrl_Init(void)
{
  CAN_FilterTypeDef filter;

  filter.FilterBank = 0;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterIdHigh = 0x0000U;
  filter.FilterIdLow = 0x0000U;
  filter.FilterMaskIdHigh = 0x0000U;
  filter.FilterMaskIdLow = 0x0000U;
  filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  filter.FilterActivation = ENABLE;
  filter.SlaveStartFilterBank = 14;

  if (HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK)
  {
    printf("CAN filter failed, error=0x%08lX\r\n", (unsigned long)HAL_CAN_GetError(&hcan));
    Error_Handler();
  }

  if (HAL_CAN_Start(&hcan) != HAL_OK)
  {
    printf("CAN start failed, error=0x%08lX\r\n", (unsigned long)HAL_CAN_GetError(&hcan));
    Error_Handler();
  }

  printf("CAN ready, current gain level=%u\r\n", (unsigned)current_gain_level);
}

static void CAN_GainCtrl_ReceiveProcess(void)
{
  CAN_RxHeaderTypeDef rx_header;
  uint8_t rx_data[8] = {0};

  while (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) > 0U)
  {
    if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK)
    {
      printf("RX failed, error=0x%08lX\r\n", (unsigned long)HAL_CAN_GetError(&hcan));
      return;
    }

    CAN_GainCtrl_PrintFrame(&rx_header, rx_data);
    CAN_GainCtrl_ParseFrame(&rx_header, rx_data);
  }
}

static void CAN_GainCtrl_PrintFrame(const CAN_RxHeaderTypeDef *rx_header, const uint8_t *rx_data)
{
  printf("RX id=0x%03lX ide=%lu rtr=%lu dlc=%lu data=",
         (unsigned long)rx_header->StdId,
         (unsigned long)rx_header->IDE,
         (unsigned long)rx_header->RTR,
         (unsigned long)rx_header->DLC);

  for (uint8_t i = 0U; (i < rx_header->DLC) && (i < 8U); i++)
  {
    printf("%02X ", rx_data[i]);
  }
  printf("\r\n");
}

static void CAN_GainCtrl_ParseFrame(const CAN_RxHeaderTypeDef *rx_header, uint8_t *rx_data)
{
  if ((rx_header->IDE != CAN_ID_STD) ||
      (rx_header->RTR != CAN_RTR_DATA) ||
      (rx_header->StdId != GAIN_CAN_STD_ID) ||
      (rx_header->DLC < 4U))
  {
    return;
  }

  uint8_t target_addr = rx_data[0];
  uint8_t cmd = rx_data[1];
  uint8_t zynq_old_level = rx_data[2];
  uint8_t zynq_new_level = rx_data[3];
  uint32_t max_mean_sq = 0U;

  if (rx_header->DLC >= 8U)
  {
    max_mean_sq = ((uint32_t)rx_data[4]) |
                  ((uint32_t)rx_data[5] << 8U) |
                  ((uint32_t)rx_data[6] << 16U) |
                  ((uint32_t)rx_data[7] << 24U);
  }

  if (target_addr != GAIN_CAN_TARGET_ADDR)
  {
    printf("gain frame ignored: addr=0x%02X\r\n", target_addr);
    return;
  }

  if ((cmd != GAIN_CAN_CMD_DOWN) &&
      (cmd != GAIN_CAN_CMD_UP) &&
      (cmd != GAIN_CAN_CMD_SET))
  {
    printf("gain frame ignored: cmd=0x%02X\r\n", cmd);
    return;
  }

  if ((zynq_new_level < GAIN_LEVEL_MIN) || (zynq_new_level > GAIN_LEVEL_MAX))
  {
    printf("gain frame ignored: bad level=%u\r\n", (unsigned)zynq_new_level);
    return;
  }

  if (zynq_old_level != current_gain_level)
  {
    printf("gain level sync: stm32=%u zynq_old=%u\r\n",
           (unsigned)current_gain_level,
           (unsigned)zynq_old_level);
  }

  Gain_SetLevel(zynq_new_level);

  printf("gain apply cmd=0x%02X old=%u new=%u rms_sq=%lu\r\n",
         (unsigned)cmd,
         (unsigned)zynq_old_level,
         (unsigned)current_gain_level,
         (unsigned long)max_mean_sq);
}
#endif

static void Gain_SetLevel(uint8_t level)
{
  uint16_t active_low_pin = GPIO_PIN_7;

  if (level < GAIN_LEVEL_MIN)
  {
    level = GAIN_LEVEL_MIN;
  }
  else if (level > GAIN_LEVEL_MAX)
  {
    level = GAIN_LEVEL_MAX;
  }

  switch (level)
  {
    case 1U:
      active_low_pin = GPIO_PIN_9;
      break;
    case 2U:
      active_low_pin = GPIO_PIN_6;
      break;
    case 3U:
      active_low_pin = GPIO_PIN_8;
      break;
    case 4U:
    default:
      active_low_pin = GPIO_PIN_7;
      break;
  }

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOB, active_low_pin, GPIO_PIN_RESET);
  current_gain_level = level;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */

  printf("Error_Handler\r\n");

  __disable_irq();

  while (1)
  {
  }

  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */

  printf("Assert failed: file %s, line %lu\r\n", file, line);

  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

