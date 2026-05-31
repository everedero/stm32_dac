/*
 * DAC loopback self-test — sine on PA4, captured back on PA5.
 *
 * Tune the three parameters below, flash, and read PASS/FAIL on UART.
 * Each metric (p2p, rms, crest factor, frequency, DC offset) is checked
 * against its theoretical value with a configurable tolerance.
 */

#include <zephyr/kernel.h>
#include <zephyr/audio/codec.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

LOG_MODULE_REGISTER(dac_loopback, LOG_LEVEL_INF);

/* ── User-tunable parameters ─────────────────────────────────────────────── */

#define SINE_FREQ_HZ    440   /* generated frequency, Hz                      */
#define SINE_AMPLITUDE  200   /* peak amplitude in 12-bit DAC counts (0–2047) */
#define TOLERANCE_PCT   5     /* ±% tolerance applied to every metric         */

/* ── DAC / sine generator ────────────────────────────────────────────────── */

#define SAMPLE_RATE    48000U
#define BLOCK_SAMPLES  256U
#define BLOCK_BYTES    (BLOCK_SAMPLES * sizeof(uint16_t))
#define FREQ_STEP      ((uint32_t)(((uint64_t)SINE_FREQ_HZ << 32) / SAMPLE_RATE))
#define DAC_MIDPOINT   2048

static const struct device *codec_dev = DEVICE_DT_GET(DT_ALIAS(codec0));

static uint16_t audio_bufs[2][BLOCK_SAMPLES];
static uint8_t  active_buf;
static uint32_t phase_acc;
static volatile uint32_t dac_callbacks;
static uint16_t sine_table[256];

static void build_sine_table(void)
{
	for (int i = 0; i < 256; i++) {
		sine_table[i] = (uint16_t)(DAC_MIDPOINT +
			SINE_AMPLITUDE * sin(2.0 * M_PI * i / 256.0));
	}
}

static void fill_buffer(uint16_t *buf)
{
	for (uint32_t i = 0; i < BLOCK_SAMPLES; i++) {
		buf[i] = sine_table[(uint8_t)(phase_acc >> 24)];
		phase_acc += FREQ_STEP;
	}
}

static void tx_done(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);
	dac_callbacks++;
	fill_buffer(audio_bufs[active_buf]);
	audio_codec_write(dev, (uint8_t *)audio_bufs[active_buf], BLOCK_BYTES);
	active_buf ^= 1;
}

/* ── ADC loopback capture & self-test ────────────────────────────────────── */

#define ADC_CHANNEL_ID      5
#define ADC_RESOLUTION      12
#define CAPTURE_SAMPLES     256
#define ADC_INTER_SAMPLE_US 100U   /* 100 µs → ~10 kHz; 256 samples = 25.6 ms */

/* Derived expected values */
#define EXP_P2P    (2.0f * SINE_AMPLITUDE)
#define EXP_RMS    ((float)SINE_AMPLITUDE / 1.41421356f)
#define EXP_CREST  1.41421356f
#define EXP_MEAN   ((float)DAC_MIDPOINT)
#define EXP_FREQ   ((float)SINE_FREQ_HZ)
#define TOL        (TOLERANCE_PCT / 100.0f)

static const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc1));

static const struct adc_channel_cfg adc_ch_cfg = {
	.gain             = ADC_GAIN_1,
	.reference        = ADC_REF_INTERNAL,
	.acquisition_time = ADC_ACQ_TIME_DEFAULT,
	.channel_id       = ADC_CHANNEL_ID,
};

static bool in_tol(float val, float exp)
{
	float margin = exp * TOL;

	return (val >= exp - margin) && (val <= exp + margin);
}

static void capture_and_check(void)
{
	static int16_t samples[CAPTURE_SAMPLES];
	struct adc_sequence seq = {
		.channels    = BIT(ADC_CHANNEL_ID),
		.buffer_size = sizeof(int16_t),
		.resolution  = ADC_RESOLUTION,
	};

	int64_t t0 = k_uptime_get();

	for (int i = 0; i < CAPTURE_SAMPLES; i++) {
		seq.buffer = &samples[i];
		if (adc_read(adc_dev, &seq) < 0) {
			LOG_ERR("FAIL: adc_read[%d] error", i);
			return;
		}
		k_busy_wait(ADC_INTER_SAMPLE_US);
	}

	int64_t t1 = k_uptime_get();

	/* ── Amplitude metrics ── */
	int16_t s_min = INT16_MAX, s_max = INT16_MIN;
	int32_t sum = 0;

	for (int i = 0; i < CAPTURE_SAMPLES; i++) {
		if (samples[i] < s_min) s_min = samples[i];
		if (samples[i] > s_max) s_max = samples[i];
		sum += samples[i];
	}

	float mean  = (float)sum / CAPTURE_SAMPLES;
	float p2p   = (float)(s_max - s_min);
	float sum_sq = 0.0f;

	for (int i = 0; i < CAPTURE_SAMPLES; i++) {
		float d = samples[i] - mean;
		sum_sq += d * d;
	}
	float rms   = sqrtf(sum_sq / CAPTURE_SAMPLES);
	float crest = (rms > 0.5f) ? (p2p / 2.0f) / rms : 0.0f;

	/* ── Frequency via zero-crossing count ── */
	int crossings = 0;

	for (int i = 1; i < CAPTURE_SAMPLES; i++) {
		if ((samples[i - 1] < mean) != (samples[i] < mean)) {
			crossings++;
		}
	}
	float capture_s = (float)(t1 - t0) / 1000.0f;
	float est_freq  = (float)crossings / (2.0f * capture_s);

	/* ── Tolerance checks ── */
	bool ok_p2p   = in_tol(p2p,   EXP_P2P);
	bool ok_rms   = in_tol(rms,   EXP_RMS);
	bool ok_crest = in_tol(crest, EXP_CREST);
	bool ok_mean  = in_tol(mean,  EXP_MEAN);
	bool ok_freq  = in_tol(est_freq, EXP_FREQ);
	bool all_pass = ok_p2p && ok_rms && ok_crest && ok_mean && ok_freq;

	int crest_i = (int)(crest * 1000.0f);
	int freq_i  = (int)est_freq;

	if (all_pass) {
		LOG_INF("PASS  p2p=%d rms=%d crest=%d.%03d freq=%dHz mean=%d",
			(int)p2p, (int)rms,
			crest_i / 1000, crest_i % 1000,
			freq_i, (int)mean);
	} else {
		LOG_ERR("FAIL  p2p=%d[%s] rms=%d[%s] crest=%d.%03d[%s] freq=%dHz[%s] mean=%d[%s]",
			(int)p2p,   ok_p2p   ? "ok" : "!",
			(int)rms,   ok_rms   ? "ok" : "!",
			crest_i / 1000, crest_i % 1000, ok_crest ? "ok" : "!",
			freq_i,     ok_freq  ? "ok" : "!",
			(int)mean,  ok_mean  ? "ok" : "!");
		LOG_ERR("  expected: p2p=%d rms=%d crest=1.414 freq=%dHz mean=%d (tol +/-%d%%)",
			(int)EXP_P2P, (int)EXP_RMS, SINE_FREQ_HZ, DAC_MIDPOINT, TOLERANCE_PCT);
	}
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(void)
{
	int ret;

	if (!device_is_ready(codec_dev)) {
		LOG_ERR("codec device not ready");
		return -ENODEV;
	}
	if (!device_is_ready(adc_dev)) {
		LOG_ERR("ADC device not ready");
		return -ENODEV;
	}

	ret = adc_channel_setup(adc_dev, &adc_ch_cfg);
	if (ret < 0) {
		LOG_ERR("adc_channel_setup failed: %d", ret);
		return ret;
	}

	build_sine_table();

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

	fill_buffer(audio_bufs[0]);
	fill_buffer(audio_bufs[1]);
	active_buf = 0;

	ret = audio_codec_write(codec_dev, (uint8_t *)audio_bufs[0], BLOCK_BYTES);
	if (ret < 0) {
		LOG_ERR("audio_codec_write (prime) failed: %d", ret);
		return ret;
	}
	ret = audio_codec_write(codec_dev, (uint8_t *)audio_bufs[1], BLOCK_BYTES);
	if (ret < 0) {
		LOG_ERR("audio_codec_write (pre-queue) failed: %d", ret);
		return ret;
	}

	ret = audio_codec_start(codec_dev, AUDIO_DAI_DIR_TX);
	if (ret < 0) {
		LOG_ERR("audio_codec_start failed: %d", ret);
		return ret;
	}

	LOG_INF("loopback self-test: %d Hz, amp=%d, tol=%d%%",
		SINE_FREQ_HZ, SINE_AMPLITUDE, TOLERANCE_PCT);

	while (true) {
		k_sleep(K_SECONDS(1));
		capture_and_check();
	}

	return 0;
}
