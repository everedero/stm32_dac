#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
//#include <zephyr/drivers/dac.h>
//#include "stm32f7xx_hal.h"
#include "stm32f7xx_ll_dac.h" // Also called in driver
#include "stm32f7xx_hal_dac.h"
#include "stm32f7xx_hal_cortex.h" // for HAL_NVIC EnableIRQ and HAL_NVIC SetPriority

DAC_HandleTypeDef hdac1;

void Error_Handler(void);
static void MX_DAC1_Init(void);
static void MX_DMA_Init(void);

int init_dac(void)
{

	uint32_t DAC_OUT[4] = {0, 1241, 2482, 3723};
	uint8_t i = 0;

//	HAL_Init();
//	SystemClock_Config();
//	MX_GPIO_Init();
	MX_DAC1_Init();
	MX_DMA_Init();
//	DAC1, channel 1 (PA4) is initialized via Zephyr API
	/* Clocks, pinctrl, channel count and resolution
	 * handled by Zephyr driver in drivers/dac/dac_stm32.c
	 * */

	while (1)
	{
		DAC1->DHR12R1 = DAC_OUT[i++];
		if(i == 4)
			i = 0;
		//HAL_Delay(50); // STM32 50ms systick delay
		k_msleep(50);
	}
		return 0;
}

void SystemClock_Config(void)
{
/*
dac1: dac@40007400 {
	compatible = "st,stm32-dac";
	reg = <0x40007400 0x400>;
	clocks = <&rcc STM32_CLOCK_BUS_APB1 0x20000000>;
	status = "disabled";
	#io-channel-cells = <1>;
};
*/
/*
rcc: rcc@40023800 {
	compatible = "st,stm32-rcc";
	#clock-cells = <2>;
	reg = <0x40023800 0x400>;
*/
}

static void MX_DAC1_Init(void)
{
	DAC_ChannelConfTypeDef sConfig = {0};

/** DAC Initialization
 * Already done?
*/
	hdac1.Instance = DAC1;
/*if (HAL_DAC_Init(&hdac1) != HAL_OK)
{
      Error_Handler();
}*/
/** DAC channel OUT1 config
*/
/*
sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK)
{
      Error_Handler();
}
*/
}


static void MX_DMA_Init(void)
{
  // DMA1 enabled as a whole, but channel 3 no

  __HAL_RCC_DMA1_CLK_ENABLE();
  //DMA1_Stream3_IRQn
  HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);

}

static void MX_GPIO_Init(void)
{
	/* GPIO Ports Clock Enable */
	//__HAL_RCC_GPIOA_CLK_ENABLE();
}

void Error_Handler(void)
{
}

int run_dac(void)
{
	return 0;
}
