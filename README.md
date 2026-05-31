# fah_dac - Zephyr audio samples for FAH v3

Zephyr module targeting the FAH v3 board (STM32F730Z8).
Demonstrates DMA-driven audio output through the on-chip DAC on PA4,
using the `audio_codec` API with a custom STM32 DAC driver.

## Hardware

| Signal | Pin  | Notes                          |
|--------|------|--------------------------------|
| DAC1   | PA4  | Audio output (3.3 V reference) |
| DMA1   | S5C7 | DAC trigger via TIM6 TRGO      |
| UART4  | –    | Console / shell                |

For the loopback test, connect PA5 (TP108) to PA4 (TP93).

## Prerequisites

```bash
# Install west and a Zephyr SDK, then initialise the workspace:
west init -m https://github.com/everedero/fah_dac --mr main zephyr_workspace
cd zephyr_workspace
west update
```

## Build instructions

All apps are built from inside the `zephyr_workspace` directory with:

```bash
west build -b fahv3 fah_dac/<app>
```

### dac\_audio: 440 Hz sine wave

Generates a continuous 440 Hz sine wave using a 256-point lookup table
and a Q16.16 DDS phase accumulator.

```bash
west build -b fahv3 fah_dac/dac_audio
```

Expected log output (~187 DMA callbacks/s at 48 kHz / 256 samples):

```
[00:00:00.123] INF: 440 Hz sine running at 48000 Hz on PA4
[00:00:01.123] INF: DMA callbacks/s: 187
```

### ptttl\_audio - RTTTL ringtone player

Parses an RTTTL string at runtime using the
[eriknyquist/ptttl](https://github.com/eriknyquist/ptttl) library and
streams PCM samples to the DAC.  The Tetris theme loops continuously.

```bash
west build -b fahv3 fah_dac/ptttl_audio
```

## Flashing

```bash
west flash
```

Press the reset button on the board after `west flash` initiates the
download if required by your debug probe setup.

In order to flash automatically, don’t connect your ST-LINK reset to the debug
header. Solder a cable on the top of the reset button footprint, which is connected
to the STM32 nRST, and connect your debug header here.

## lib/ptttl

A thin Zephyr library wrapping the upstream C implementation of
[eriknyquist/ptttl](https://github.com/eriknyquist/ptttl) (MIT licence).

Capabilities:
- Parses RTTTL and PTTTL (polyphonic superset) strings
- Generates `int16_t` PCM samples - no dynamic memory, no `math.h`
- Waveforms: sine (fast polynomial), triangle, sawtooth, square, Nokia 3310

Enable in your app's `prj.conf`:

```kconfig
CONFIG_PTTTL=y
```

Minimal usage:

```c
#include "ptttl_parser.h"
#include "ptttl_sample_generator.h"

static const char rtttl[] = "MySong:d=4,o=5,b=120:e,g,a";
static uint32_t pos;

static int my_read(char ch) {
    if (pos >= sizeof(rtttl) - 1) return 1;
    ch = rtttl[pos++]; return 0;
}
static int my_seek(uint32_t p) { pos = p; return 0; }

ptttl_parser_t parser;
ptttl_sample_generator_t gen;
ptttl_sample_generator_config_t cfg = PTTTL_SAMPLE_GENERATOR_CONFIG_DEFAULT;
cfg.sample_rate = 48000;

ptttl_parse_init(&parser, (ptttl_parser_input_iface_t){ my_read, my_seek });
ptttl_sample_generator_create(&parser, &gen, &cfg);

int16_t buf[256];
uint32_t n = 256;
while (ptttl_sample_generator_generate(&gen, &n, buf) == 0) {
    / send buf[0..n-1] to your audio output /
    n = 256;
}
```

## Licence

Application code: Apache-2.0
`lib/ptttl` (eriknyquist/ptttl): MIT
