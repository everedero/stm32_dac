#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
//#include <zephyr/drivers/dac.h>
//#include "stm32f7xx_hal.h"
#include "stm32f7xx_ll_dac.h" // Also called in driver
#include "stm32f7xx_hal_dac.h"
#include "stm32f7xx_hal_cortex.h" // for HAL_NVIC EnableIRQ and HAL_NVIC SetPriority

DAC_HandleTypeDef hdac1;
DMA_HandleTypeDef hdma_dac1;
TIM_HandleTypeDef htim2;

void Error_Handler(void);
static void MX_DAC1_Init(void);
static void MX_DMA_Init(void);

#define NS  128

int init_dac(void)
{

	uint32_t DAC_OUT[4] = {0, 1241, 2482, 3723};
	uint32_t Wave_LUT[NS] = {
		2048, 2149, 2250, 2350, 2450, 2549, 2646, 2742, 2837, 2929, 3020, 3108, 3193, 3275, 3355,
		3431, 3504, 3574, 3639, 3701, 3759, 3812, 3861, 3906, 3946, 3982, 4013, 4039, 4060, 4076,
		4087, 4094, 4095, 4091, 4082, 4069, 4050, 4026, 3998, 3965, 3927, 3884, 3837, 3786, 3730,
		3671, 3607, 3539, 3468, 3394, 3316, 3235, 3151, 3064, 2975, 2883, 2790, 2695, 2598, 2500,
		2400, 2300, 2199, 2098, 1997, 1896, 1795, 1695, 1595, 1497, 1400, 1305, 1212, 1120, 1031,
		944, 860, 779, 701, 627, 556, 488, 424, 365, 309, 258, 211, 168, 130, 97,
		69, 45, 26, 13, 4, 0, 1, 8, 19, 35, 56, 82, 113, 149, 189,
		234, 283, 336, 394, 456, 521, 591, 664, 740, 820, 902, 987, 1075, 1166, 1258,
		1353, 1449, 1546, 1645, 1745, 1845, 1946, 2047
	};
	uint8_t i = 0;
	uint32_t err = 0;

//	HAL_Init();
//	SystemClock_Config();
//	MX_GPIO_Init();
	MX_DAC1_Init();
	MX_DMA_Init();
//	DAC1, channel 1 (PA4) is initialized via Zephyr API
	/* Clocks, pinctrl, channel count and resolution
	 * handled by Zephyr driver in drivers/dac/dac_stm32.c
	 * */

	err = HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t*)Wave_LUT, 128, DAC_ALIGN_12B_R);
	if (err != HAL_OK) {
		printk(err);
	 }
	HAL_TIM_Base_Start(&htim2);

/*	while (1)
	{
		DAC1->DHR12R1 = DAC_OUT[i++];
		if(i == 4)
			i = 0;
		//HAL_Delay(50); // STM32 50ms systick delay
		k_msleep(50);
	}*/
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
	if (HAL_DAC_Init(&hdac1) != HAL_OK)
	{
	      Error_Handler();
	}
	/** DAC channel OUT1 config
	*/

	//sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
	sConfig.DAC_Trigger = DAC_TRIGGER_T2_TRGO;
	sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
	if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK)
	{
	      Error_Handler();
	}
	/* Enable DMA */
	/* DAC DMA Init */
	/* DAC1 Init */
	hdma_dac1.Instance = DMA1_Stream5;
	hdma_dac1.Init.Channel = DMA_CHANNEL_7;
	hdma_dac1.Init.Direction = DMA_MEMORY_TO_PERIPH;
	hdma_dac1.Init.PeriphInc = DMA_PINC_DISABLE;
	hdma_dac1.Init.MemInc = DMA_MINC_ENABLE;
	hdma_dac1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
	hdma_dac1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
	hdma_dac1.Init.Mode = DMA_NORMAL;
	hdma_dac1.Init.Priority = DMA_PRIORITY_LOW;
	hdma_dac1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
	if (HAL_DMA_Init(&hdma_dac1) != HAL_OK)
	{
	  Error_Handler();
	}
	hdac1.DMA_Handle1 = &hdma_dac1;
	hdma_dac1.Parent = &hdac1;
    	//__HAL_LINKDMA(hdac1, DMA_Handle1, hdma_dac1);
	// Manual trigger source
	LL_DAC_SetTriggerSource(DAC1, 1, LL_DAC_TRIG_EXT_TIM2_TRGO);
}


static void MX_DMA_Init(void)
{
  // DMA1 enabled as a whole, but channel 5 no

  __HAL_RCC_DMA1_CLK_ENABLE();
  //DMA1_Stream5_IRQn
  //HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 0, 0);
  //HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);

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
