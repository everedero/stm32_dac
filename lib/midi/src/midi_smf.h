/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>

#define MIDI_MAX_EVENTS 1024

typedef enum {
	MIDI_EV_NOTE_ON = 0,
	MIDI_EV_NOTE_OFF,
	MIDI_EV_TEMPO,
} midi_event_type_t;

typedef struct {
	uint32_t tick;
	uint8_t  type;      /* midi_event_type_t */
	uint8_t  channel;
	uint8_t  note;
	uint8_t  velocity;
	uint32_t tempo_us;  /* only valid for MIDI_EV_TEMPO */
} midi_event_t;

typedef struct {
	midi_event_t events[MIDI_MAX_EVENTS];
	uint32_t     count;
	uint16_t     ticks_per_beat;
} midi_song_t;

/*
 * Parse a raw SMF byte array into a midi_song_t.
 * Supports Format 0 (single track) and Format 1 (multi-track, merged by tick).
 * Returns 0 on success, -1 on error.
 */
int midi_smf_parse(midi_song_t *song, const uint8_t *data, uint32_t len);
