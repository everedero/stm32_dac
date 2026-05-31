/* SPDX-License-Identifier: MIT */
#include "midi_synth.h"
#include <math.h>
#include <string.h>
#include <stdint.h>

#define MIDI_SYNTH_VOICES  8
#define ATTACK_SAMPLES     200    /* ~4 ms at 48 kHz */
#define RELEASE_SAMPLES    800    /* ~17 ms */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── DDS sine table (float, ±1.0) ────────────────────────────────────────── */

static float sine_f[256];

/* ── Per-note phase step lookup (128 MIDI notes) ─────────────────────────── */

static uint32_t note_step[128];

/* ── Voice state ─────────────────────────────────────────────────────────── */

typedef struct {
	uint32_t phase_acc;
	uint32_t phase_step;
	uint8_t  note;
	uint8_t  channel;
	bool     active;
	bool     releasing;
	float    env;
	float    env_atk_rate;    /* per sample */
	float    env_rel_rate;    /* per sample */
	float    amplitude;       /* 0.0 – 1.0 from velocity */
	uint32_t age;             /* for voice stealing */
} voice_t;

static voice_t voices[MIDI_SYNTH_VOICES];
static uint32_t voice_age_ctr;

/* ── Song timeline state ──────────────────────────────────────────────────── */

static const midi_song_t *song_ref;
static uint32_t           sample_rate_ref;
static uint32_t            sample_pos;
static uint32_t            event_idx;

/* Tempo tracking: recomputed on MIDI_EV_TEMPO events */
static uint32_t tempo_tick_base;
static uint32_t tempo_sample_base;
static float    samples_per_tick;

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static uint32_t tick_to_sample(uint32_t tick)
{
	return tempo_sample_base +
	       (uint32_t)((tick - tempo_tick_base) * samples_per_tick);
}

static voice_t *find_free_voice(void)
{
	for (int i = 0; i < MIDI_SYNTH_VOICES; i++) {
		if (!voices[i].active) {
			return &voices[i];
		}
	}
	return NULL;
}

static voice_t *steal_voice(void)
{
	/* Prefer a releasing voice with lowest envelope first */
	voice_t *best = NULL;

	for (int i = 0; i < MIDI_SYNTH_VOICES; i++) {
		if (voices[i].releasing) {
			if (!best || voices[i].env < best->env) {
				best = &voices[i];
			}
		}
	}
	if (best) {
		return best;
	}
	/* Fall back to oldest active voice */
	best = &voices[0];
	for (int i = 1; i < MIDI_SYNTH_VOICES; i++) {
		if (voices[i].age < best->age) {
			best = &voices[i];
		}
	}
	return best;
}

static void note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
	voice_t *v = find_free_voice();

	if (!v) {
		v = steal_voice();
	}

	v->phase_acc     = 0;
	v->phase_step    = note_step[note & 0x7F];
	v->note          = note;
	v->channel       = channel;
	v->active        = true;
	v->releasing     = false;
	v->env           = 0.0f;
	v->env_atk_rate  = 1.0f / ATTACK_SAMPLES;
	v->env_rel_rate  = 1.0f / RELEASE_SAMPLES;
	v->amplitude     = (float)velocity / 127.0f;
	v->age           = voice_age_ctr++;
}

static void note_off(uint8_t channel, uint8_t note)
{
	for (int i = 0; i < MIDI_SYNTH_VOICES; i++) {
		if (voices[i].active && !voices[i].releasing &&
		    voices[i].note == note && voices[i].channel == channel) {
			voices[i].releasing = true;
			return;
		}
	}
}

static void apply_tempo(const midi_event_t *ev)
{
	/* Record current position as new tempo baseline */
	tempo_tick_base   = ev->tick;
	tempo_sample_base = tick_to_sample(ev->tick);
	samples_per_tick  = (float)ev->tempo_us / 1e6f *
			    (float)sample_rate_ref /
			    (float)song_ref->ticks_per_beat;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int midi_synth_init(const midi_song_t *song, uint32_t sample_rate)
{
	song_ref        = song;
	sample_rate_ref = sample_rate;

	/* Build sine table */
	for (int i = 0; i < 256; i++) {
		sine_f[i] = sinf(2.0f * (float)M_PI * i / 256.0f);
	}

	/* Build note→phase_step table */
	for (int n = 0; n < 128; n++) {
		float freq  = 440.0f * powf(2.0f, (n - 69) / 12.0f);
		note_step[n] = (uint32_t)(freq / (float)sample_rate * 4294967296.0f);
	}

	midi_synth_restart();
	return 0;
}

void midi_synth_restart(void)
{
	memset(voices, 0, sizeof(voices));
	voice_age_ctr     = 0;
	sample_pos        = 0;
	event_idx         = 0;
	tempo_tick_base   = 0;
	tempo_sample_base = 0;

	/* Default tempo: 120 BPM = 500 000 µs/beat */
	samples_per_tick = 500000.0f / 1e6f *
			   (float)sample_rate_ref /
			   (float)song_ref->ticks_per_beat;

	/* Apply any tempo event at tick 0 */
	while (event_idx < song_ref->count &&
	       song_ref->events[event_idx].tick == 0 &&
	       song_ref->events[event_idx].type == MIDI_EV_TEMPO) {
		apply_tempo(&song_ref->events[event_idx]);
		event_idx++;
	}
}

bool midi_synth_fill(int16_t *buf, uint32_t n)
{
	bool ended = false;

	for (uint32_t i = 0; i < n; i++) {
		/* Fire all events due at or before this sample */
		while (event_idx < song_ref->count &&
		       tick_to_sample(song_ref->events[event_idx].tick) <= sample_pos) {
			const midi_event_t *ev = &song_ref->events[event_idx];

			switch (ev->type) {
			case MIDI_EV_NOTE_ON:
				note_on(ev->channel, ev->note, ev->velocity);
				break;
			case MIDI_EV_NOTE_OFF:
				note_off(ev->channel, ev->note);
				break;
			case MIDI_EV_TEMPO:
				apply_tempo(ev);
				break;
			}
			event_idx++;
		}

		/* Check end of song: all events processed and all voices silent */
		if (event_idx >= song_ref->count) {
			bool any_active = false;

			for (int v = 0; v < MIDI_SYNTH_VOICES; v++) {
				if (voices[v].active) {
					any_active = true;
					break;
				}
			}
			if (!any_active) {
				/* Zero-fill remaining samples */
				for (uint32_t j = i; j < n; j++) {
					buf[j] = 0;
				}
				ended = true;
				break;
			}
		}

		/* Synthesize — sum all active voices */
		float sum = 0.0f;

		for (int v = 0; v < MIDI_SYNTH_VOICES; v++) {
			voice_t *vp = &voices[v];

			if (!vp->active) {
				continue;
			}

			/* Update envelope */
			if (vp->releasing) {
				vp->env -= vp->env_rel_rate;
				if (vp->env <= 0.0f) {
					vp->env    = 0.0f;
					vp->active = false;
					continue;
				}
			} else if (vp->env < 1.0f) {
				vp->env += vp->env_atk_rate;
				if (vp->env > 1.0f) {
					vp->env = 1.0f;
				}
			}

			/* DDS sample */
			float s = sine_f[vp->phase_acc >> 24] * vp->amplitude * vp->env;

			vp->phase_acc += vp->phase_step;
			sum += s;
		}

		/* Clamp to int16 range — full amplitude for single voice */
		if (sum >  1.0f) sum =  1.0f;
		if (sum < -1.0f) sum = -1.0f;
		buf[i] = (int16_t)(sum * 32767.0f);

		sample_pos++;
	}

	return ended;
}
