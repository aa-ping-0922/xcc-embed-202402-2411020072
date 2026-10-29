/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 3×4 矩阵键盘行扫描 + 8421 BCD 码指示灯显示
  ******************************************************************************
  * 硬件接法（依据原理图 Key / LED 分区解析）：
  *   行：PE4 = K1~K4    PE5 = K5~K8    PE6 = K9~K12   输出，扫描时拉低
  *   列：PE0 / PE1 / PE2 / PE3                          上拉输入，按下读低
  *   灯：PF0 = L1(bit0)  PF1 = L2(bit1)  PF2 = L3(bit2)  PF3 = L4(bit3)
  *       每路串 680R 接 +3.3V，因此低电平点亮
  *
  * 例：按 K9 → 键值 9 → 8421 码 1001 → L1 与 L4 点亮
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* 1：低电平点亮；0：高电平点亮 */
#define LED_ACTIVE_LOW        1U

#define LED_ALL_PINS          (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3)
#define KEY_ROW_ALL_PINS      (GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6)

#define KEY_NONE              0U
#define KEY_INVALID           0xFFU
#define KEY_DEBOUNCE_MS       20U

/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* 扫描用的行、列引脚表 */
static const uint16_t key_row_pins[3] = { GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6 };
static const uint16_t key_col_pins[4] = { GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3 };

/* 当前要显示的十进制数，取值 0~9 */
static uint8_t display_value = 0U;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/* USER CODE BEGIN 0 */

/**
  * @brief  把 4 位二进制写到 L1~L4。
  * @param  bits bit0→L1，bit1→L2，bit2→L3，bit3→L4，位为 1 表示点亮
  * @note   本函数只关心“亮/灭”，不关心有效电平，极性由 LED_ACTIVE_LOW 决定。
  */
static void LED_WriteBits(uint8_t bits)
{
    static const uint16_t led_pins[4] = { GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3 };
    uint8_t i;

    for (i = 0U; i < 4U; i++)
    {
        uint8_t on = (uint8_t)((bits >> i) & 0x01U);

#if (LED_ACTIVE_LOW != 0U)
        HAL_GPIO_WritePin(GPIOF, led_pins[i], (on != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
#else
        HAL_GPIO_WritePin(GPIOF, led_pins[i], (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
#endif
    }
}

/**
  * @brief  显示一位十进制数（8421 BCD）。
  * @param  digit 0~9，超出范围一律全灭
  */
static void LED_ShowDigit(uint8_t digit)
{
    if (digit <= 9U)
    {
        LED_WriteBits(digit);
    }
    else
    {
        LED_WriteBits(0U);
    }
}

/**
  * @brief  完成一轮 3 行扫描。
  * @retval 0    没有键按下
  * @retval 1~12 检测到一个键，编号方式与板上印字一致
  * @retval 0xFF 同时检测到多个闭合点，视为无效
  */
static uint8_t Keypad_ScanRaw(void)
{
    uint8_t row;
    uint8_t col;
    uint8_t count = 0U;
    uint8_t key = KEY_NONE;

    /* 先释放所有行 */
    HAL_GPIO_WritePin(GPIOE, KEY_ROW_ALL_PINS, GPIO_PIN_SET);

    for (row = 0U; row < 3U; row++)
    {
        /* 当前行拉低，其余行保持释放 */
        HAL_GPIO_WritePin(GPIOE, key_row_pins[row], GPIO_PIN_RESET);

        /* 等列线电平建立 */
        HAL_Delay(1U);

        for (col = 0U; col < 4U; col++)
        {
            if (HAL_GPIO_ReadPin(GPIOE, key_col_pins[col]) == GPIO_PIN_RESET)
            {
                count++;
                key = (uint8_t)(row * 4U + col + 1U);
            }
        }

        /* 本行扫描结束，立即释放 */
        HAL_GPIO_WritePin(GPIOE, key_row_pins[row], GPIO_PIN_SET);
    }

    if (count == 0U)
    {
        return KEY_NONE;
    }
    if (count == 1U)
    {
        return key;
    }
    return KEY_INVALID;
}

/**
  * @brief  取经过消抖的“新按下”事件。
  * @retval 0 无事件；1~12 一次按下；多键时返回 0 并要求全部松开
  * @note   长按只返回一次；再次触发必须先全部松开。
  */
static uint8_t Keypad_GetEvent(void)
{
    static uint8_t  candidate = KEY_NONE;
    static uint8_t  stable = KEY_NONE;
    static uint8_t  armed = 1U;
    static uint32_t candidate_tick = 0U;

    uint8_t  raw = Keypad_ScanRaw();
    uint32_t now = HAL_GetTick();

    /* 状态变化：重新计时 */
    if (raw != candidate)
    {
        candidate = raw;
        candidate_tick = now;
        return KEY_NONE;
    }

    /* 尚未稳定够 20 ms */
    if ((uint32_t)(now - candidate_tick) < KEY_DEBOUNCE_MS)
    {
        return KEY_NONE;
    }

    /* 状态没变，不重复上报 */
    if (stable == candidate)
    {
        return KEY_NONE;
    }

    stable = candidate;

    if (stable == KEY_NONE)
    {
        armed = 1U;          /* 松开后允许下一次触发 */
        return KEY_NONE;
    }

    if (stable == KEY_INVALID)
    {
        armed = 0U;          /* 多键：必须全部松开才恢复 */
        return KEY_NONE;
    }

    if (armed != 0U)
    {
        armed = 0U;
        return stable;
    }

    return KEY_NONE;
}

/**
  * @brief  保存要显示的数值。
  */
static void Display_SetValue(uint8_t value)
{
    display_value = value;
}

/**
  * @brief  刷新显示。本实验只显示 0~9 一位 BCD。
  */
static void Display_Task(void)
{
    LED_ShowDigit(display_value);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  MX_GPIO_Init();

  /* USER CODE BEGIN 2 */

  /* 上电先释放所有行，避免某一行一直拉低 */
  HAL_GPIO_WritePin(GPIOE, KEY_ROW_ALL_PINS, GPIO_PIN_SET);

  /* 上电显示 0 → 8421 码 0000 → 四灯全灭 */
  Display_SetValue(0U);
  Display_Task();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint8_t key = Keypad_GetEvent();

    /* 本实验只用 K1~K9；K10~K12 扫到也忽略，保持原显示 */
    if ((key != KEY_NONE) && (key <= 9U))
    {
        Display_SetValue(key);
    }

    Display_Task();

    HAL_Delay(1U);
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

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* 可在此打印文件名与行号 */
}
#endif /* USE_FULL_ASSERT */