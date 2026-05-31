/*
 * MIDI audio sample — plays a Standard MIDI File via the STM32 DAC codec.
 *
 * The in-tree midi library parses the SMF byte array from midi_data.c and
 * synthesises polyphonic sine-wave audio using a DDS oscillator per voice.
 * The DMA/codec pipeline is the same double-buffer scheme as ptttl_audio:
 * the TX-done callback fills whichever buffer just finished and re-queues it.
 * When the song ends the synthesiser restarts automatically.
 *
 * Sample conversion: synth produces signed 16-bit centred at 0; the STM32
 * DAC expects 12-bit unsigned (0–4095, midpoint 2048).
 * Formula: dac = (int16_sample >> 4) + 2048
 */

#include <zephyr/kernel.h>
#include <zephyr/audio/codec.h>
#include <zephyr/logging/log.h>

#include "midi_smf.h"
#include "midi_synth.h"

LOG_MODULE_REGISTER(midi_audio, LOG_LEVEL_INF);

#define SAMPLE_RATE    48000U
#define BLOCK_SAMPLES  256U
#define BLOCK_BYTES    (BLOCK_SAMPLES * sizeof(uint16_t))

/* Provided by midi_data.c */
extern const uint8_t  midi_data[];
extern const uint32_t midi_data_len;

static const struct device *codec_dev = DEVICE_DT_GET(DT_ALIAS(codec0));

static uint16_t dac_bufs[2][BLOCK_SAMPLES];
static uint8_t  active_buf;
static int16_t  gen_scratch[BLOCK_SAMPLES];

static volatile uint32_t callback_count;

static void fill_dac_buffer(uint16_t *dac_buf)
{
	bool done = midi_synth_fill(gen_scratch, BLOCK_SAMPLES);

	for (uint32_t i = 0; i < BLOCK_SAMPLES; i++) {
		dac_buf[i] = (uint16_t)(((int32_t)gen_scratch[i] >> 4) + 2048);
	}

	if (done) {
		midi_synth_restart();
	}
}

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
	static midi_song_t song;
	int ret;

	if (!device_is_ready(codec_dev)) {
		LOG_ERR("codec device not ready");
		return -ENODEV;
	}

	ret = midi_smf_parse(&song, midi_data, midi_data_len);
	if (ret < 0) {
		LOG_ERR("midi_smf_parse failed: %d", ret);
		return ret;
	}
	LOG_INF("MIDI: %u events, %u ticks/beat",
		song.count, song.ticks_per_beat);

	midi_synth_init(&song, SAMPLE_RATE);

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

	LOG_INF("MIDI audio running at %u Hz on PA4", SAMPLE_RATE);

	while (true) {
		k_sleep(K_SECONDS(1));
		LOG_INF("DMA callbacks/s: %u", callback_count);
		callback_count = 0;
	}

	return 0;
}
