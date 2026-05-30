/*
 * PTTTL audio sample — plays an RTTTL ringtone string via the STM32 DAC codec.
 *
 * The PTTTL library parses the RTTTL string and generates int16_t PCM samples
 * using a fast sine approximation (no math.h dependency).  The DMA/codec
 * pipeline is the same double-buffer scheme as dac_audio: the TX-done callback
 * fills whichever buffer just finished and re-queues it.  When the ringtone
 * ends the generator is re-initialised so playback loops indefinitely.
 *
 * Sample conversion: PTTTL produces signed 16-bit centered at 0; the STM32
 * DAC expects 12-bit unsigned (0–4095, midpoint 2048).
 * Formula: dac = (int16_sample >> 4) + 2048
 */

#include <zephyr/kernel.h>
#include <zephyr/audio/codec.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "ptttl_parser.h"
#include "ptttl_sample_generator.h"

LOG_MODULE_REGISTER(ptttl_audio, LOG_LEVEL_INF);

#define SAMPLE_RATE    48000U
#define BLOCK_SAMPLES  256U
#define BLOCK_BYTES    (BLOCK_SAMPLES * sizeof(uint16_t))

static const struct device *codec_dev = DEVICE_DT_GET(DT_ALIAS(codec0));

/* Double-buffered DAC output (12-bit unsigned values in uint16_t) */
static uint16_t dac_bufs[2][BLOCK_SAMPLES];
static uint8_t  active_buf;

/* Scratch buffer for PTTTL int16_t output before DAC conversion */
static int16_t gen_scratch[BLOCK_SAMPLES];

static volatile uint32_t callback_count;

/* ------------------------------------------------------------------ */
/* RTTTL source string — Tetris theme (A-side)                        */
/* ------------------------------------------------------------------ */
static const char rtttl_str[] =
	"Tetris:d=4,o=5,b=160:"
	"e6,8b,8c6,d6,16c6,16b,a,8a,8c6,e6,8d6,8c6,b,8b,8c6,d6,e6,c6,a,2a,8p,"
	"d6,8f6,a6,8g6,8f6,e6,8e6,8c6,e6,8d6,8c6,b,8b,8c6,d6,e6,c6,a,a";

/* ------------------------------------------------------------------ */
/* PTTTL parser / generator state                                       */
/* ------------------------------------------------------------------ */
static ptttl_parser_t           ptttl_parser;
static ptttl_sample_generator_t ptttl_gen;
static ptttl_sample_generator_config_t ptttl_cfg;

/* Current read position used by the I/O callbacks */
static uint32_t rtttl_pos;
static bool     ptttl_active;

static int rtttl_read(char *ch)
{
	if (rtttl_pos >= sizeof(rtttl_str) - 1u) {
		return 1; /* end of string */
	}
	*ch = rtttl_str[rtttl_pos++];
	return 0;
}

static int rtttl_seek(uint32_t pos)
{
	rtttl_pos = pos; /* read() handles out-of-bounds on next call */
	return 0;
}

/* Initialise (or re-initialise) the PTTTL parser + generator.
 * Called from both main() and the TX-done ISR, so it must not block
 * or call LOG. */
static void ptttl_reinit(void)
{
	rtttl_pos = 0u;

	ptttl_parser_input_iface_t iface = {
		.read = rtttl_read,
		.seek = rtttl_seek,
	};

	if (ptttl_parse_init(&ptttl_parser, iface) != 0) {
		ptttl_active = false;
		return;
	}

	if (ptttl_sample_generator_create(&ptttl_parser, &ptttl_gen, &ptttl_cfg) != 0) {
		ptttl_active = false;
		return;
	}

	ptttl_active = true;
}

/* Fill one DAC buffer with the next BLOCK_SAMPLES samples.
 * Converts PTTTL signed-16 → DAC unsigned-12 in-place.
 * Loops the ringtone when the generator signals completion. */
static void fill_dac_buffer(uint16_t *dac_buf)
{
	uint32_t n = BLOCK_SAMPLES;

	if (!ptttl_active) {
		/* Output mid-rail silence */
		for (uint32_t i = 0u; i < BLOCK_SAMPLES; i++) {
			dac_buf[i] = 2048u;
		}
		return;
	}

	int ret = ptttl_sample_generator_generate(&ptttl_gen, &n, gen_scratch);

	if (ret == 1) {
		/* Generator exhausted — zero-pad tail and schedule loop */
		for (uint32_t i = n; i < BLOCK_SAMPLES; i++) {
			gen_scratch[i] = 0;
		}
		/* Reinitialise now so the next call plays from the start */
		ptttl_reinit();
	}

	/* Scale: signed 16-bit [−32768…32767] → unsigned 12-bit [0…4095] */
	for (uint32_t i = 0u; i < BLOCK_SAMPLES; i++) {
		dac_buf[i] = (uint16_t)(((int32_t)gen_scratch[i] >> 4) + 2048);
	}
}

/* DMA TX-done callback — called from ISR context */
static void tx_done(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	callback_count++;
	active_buf ^= 1u;
	fill_dac_buffer(dac_bufs[active_buf]);
	audio_codec_write(dev, (uint8_t *)dac_bufs[active_buf], BLOCK_BYTES);
}

int main(void)
{
	struct audio_codec_cfg codec_cfg = {
		.mclk_freq = 0,
		.dai_type  = AUDIO_DAI_TYPE_PCM,
		.dai_route = AUDIO_ROUTE_PLAYBACK,
		.dai_cfg.pcm = {
			.dir        = AUDIO_DAI_DIR_TX,
			.pcm_width  = AUDIO_PCM_WIDTH_16_BITS,
			.channels   = 1,
			.block_size = BLOCK_BYTES,
			.samplerate = AUDIO_PCM_RATE_48K,
		},
	};
	int ret;

	if (!device_is_ready(codec_dev)) {
		LOG_ERR("codec device not ready");
		return -ENODEV;
	}

	/* Configure the sample generator — do this before ptttl_reinit() */
	ptttl_cfg = (ptttl_sample_generator_config_t)
		PTTTL_SAMPLE_GENERATOR_CONFIG_DEFAULT;
	ptttl_cfg.sample_rate = SAMPLE_RATE;

	ptttl_reinit();
	if (!ptttl_active) {
		LOG_ERR("PTTTL init failed");
		return -EINVAL;
	}

	ret = audio_codec_configure(codec_dev, &codec_cfg);
	if (ret < 0) {
		LOG_ERR("audio_codec_configure failed: %d", ret);
		return ret;
	}

	ret = audio_codec_register_done_callback(codec_dev, tx_done, NULL, NULL, NULL);
	if (ret < 0) {
		LOG_ERR("audio_codec_register_done_callback failed: %d", ret);
		return ret;
	}

	/* Prime buffer 0 before starting the DMA */
	fill_dac_buffer(dac_bufs[0]);
	active_buf = 0u;

	ret = audio_codec_write(codec_dev, (uint8_t *)dac_bufs[0], BLOCK_BYTES);
	if (ret < 0) {
		LOG_ERR("audio_codec_write (prime) failed: %d", ret);
		return ret;
	}

	ret = audio_codec_start(codec_dev, AUDIO_DAI_DIR_TX);
	if (ret < 0) {
		LOG_ERR("audio_codec_start failed: %d", ret);
		return ret;
	}

	LOG_INF("PTTTL audio running — Tetris theme looping at %u Hz on PA4",
		SAMPLE_RATE);

	/* Print callback rate once per second; expect ~187 cb/s at 48 kHz/256 */
	while (true) {
		k_sleep(K_SECONDS(1));
		LOG_INF("DMA callbacks/s: %u", callback_count);
		callback_count = 0;
	}

	return 0;
}
