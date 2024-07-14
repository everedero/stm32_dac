# Zephyr STM32 DAC sample
## Build
```
BOARD=nucleo_f756zg
west build -b $BOARD -p always dac
```

## Hardware
DAC1: PA4 output (Nucleo D24)
DMA1 Stream 5

We verified with devmem:
* Timer activated
* DMA1 configured
* GPIO port mode register MODER bits = 0b11


## Header from STM32 Cube

    ls ../modules/hal/stm32/stm32cube/stm32f7xx/drivers/include/

## Missing linker stuff

Be careful to not call init from stm32 hal that are not already called
by Zephyr kernel automagically.

## West when folder renamed

If you renamed your example-application folder:

    west config -l

Gives you the parameter where the west.yml file inhabits.

    west config manifest.path stm32_dac
