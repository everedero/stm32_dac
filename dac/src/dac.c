#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
//#include <zephyr/drivers/dac.h>
//#include "stm32f7xx_hal.h"
#include "stm32f7xx_ll_dac.h"
#include "stm32f7xx_hal_dac.h"

DAC_HandleTypeDef hdac1;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DAC1_Init(void);

int init_dac(void)
{

    uint32_t DAC_OUT[4] = {0, 1241, 2482, 3723};
    uint8_t i = 0;

//   HAL_Init();
//    SystemClock_Config();
    MX_GPIO_Init();
    MX_DAC1_Init();

    while (1)
    {
    	DAC1->DHR12R1 = DAC_OUT[i++];
    	if(i == 4)
    		i = 0;
    	HAL_Delay(50);
    }
        return 0;
}

void SystemClock_Config(void)
{
}

static void MX_DAC1_Init(void)
{
}

static void MX_GPIO_Init(void)
{

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();

}

void Error_Handler(void)
{
}

int run_dac(void)
{

        return 0;
}
