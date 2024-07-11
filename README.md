# Zephyr STM32 DAC sample
## Build
```
BOARD=nucleo_f756zg
west build -b $BOARD -p always dac
```

## Hardware
DAC1: PA4 output (Nucleo D24)
DMA1 Stream 5

## Header from STM32 Cube

    ls ../modules/hal/stm32/stm32cube/stm32f7xx/drivers/include/

## Missing linker stuff

Be careful to not call init from stm32 hal that are not already called
by Zephyr kernel automagically.

