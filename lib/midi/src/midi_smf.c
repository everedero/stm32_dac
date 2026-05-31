/* SPDX-License-Identifier: MIT */
#include "midi_smf.h"
#include <string.h>

static uint16_t be16(const uint8_t *d, uint32_t p)
{
	return ((uint16_t)d[p] << 8) | d[p + 1];
}

static uint32_t be32(const uint8_t *d, uint32_t p)
{
	return ((uint32_t)d[p] << 24) | ((uint32_t)d[p+1] << 16) |
	       ((uint32_t)d[p+2] << 8) | d[p+3];
}

static uint32_t read_vlq(const uint8_t *d, uint32_t *p, uint32_t end)
{
	uint32_t v = 0;
	uint8_t  b;

	do {
		if (*p >= end) {
			return 0;
		}
		b = d[(*p)++];
		v = (v << 7) | (b & 0x7F);
	} while (b & 0x80);

	return v;
}

/* Parse one MTrk chunk, appending absolute-tick events to the array.
 * Returns number of events added, or -1 on error. */
static int parse_track(midi_event_t *events, uint32_t max, uint32_t *count,
		       const uint8_t *data, uint32_t start, uint32_t end)
{
	uint32_t p       = start;
	uint32_t abs_tick = 0;
	uint8_t  running_status = 0;

	while (p < end) {
		uint32_t delta = read_vlq(data, &p, end);

		abs_tick += delta;

		if (p >= end) {
			return -1;
		}

		uint8_t status = data[p];

		/* Meta event */
		if (status == 0xFF) {
			p++;
			if (p + 1 >= end) {
				return -1;
			}
			uint8_t meta_type = data[p++];
			uint32_t meta_len = read_vlq(data, &p, end);

			if (meta_type == 0x51 && meta_len == 3) {
				/* Tempo change */
				if (p + 3 > end) {
					return -1;
				}
				uint32_t us = ((uint32_t)data[p] << 16) |
					      ((uint32_t)data[p+1] << 8) |
					      data[p+2];
				if (*count < max) {
					events[*count] = (midi_event_t){
						.tick     = abs_tick,
						.type     = MIDI_EV_TEMPO,
						.tempo_us = us,
					};
					(*count)++;
				}
			} else if (meta_type == 0x2F) {
				/* End of track */
				break;
			}
			p += meta_len;
			running_status = 0;
			continue;
		}

		/* SysEx */
		if (status == 0xF0 || status == 0xF7) {
			p++;
			uint32_t sysex_len = read_vlq(data, &p, end);

			p += sysex_len;
			running_status = 0;
			continue;
		}

		/* MIDI channel event — use running status if high bit not set */
		uint8_t cmd;

		if (status & 0x80) {
			cmd = status;
			running_status = status;
			p++;
		} else {
			cmd = running_status;
		}

		uint8_t type    = cmd >> 4;
		uint8_t channel = cmd & 0x0F;

		if (p + 1 >= end && type != 0xC && type != 0xD) {
			return -1;
		}

		if (type == 0x9 || type == 0x8) {
			/* Note-on or note-off */
			uint8_t note = data[p++];
			uint8_t vel  = data[p++];
			/* note-on with velocity 0 is treated as note-off */
			midi_event_type_t ev_type =
				(type == 0x9 && vel > 0) ? MIDI_EV_NOTE_ON
							 : MIDI_EV_NOTE_OFF;
			if (*count < max) {
				events[*count] = (midi_event_t){
					.tick     = abs_tick,
					.type     = ev_type,
					.channel  = channel,
					.note     = note,
					.velocity = vel,
				};
				(*count)++;
			}
		} else if (type == 0xA || type == 0xB || type == 0xE) {
			/* Aftertouch, control change, pitch bend — 2 data bytes, skip */
			p += 2;
		} else if (type == 0xC || type == 0xD) {
			/* Program change, channel pressure — 1 data byte, skip */
			p += 1;
		} else {
			/* Unknown — bail */
			return -1;
		}
	}

	return 0;
}

/* Insertion-sort events by tick (stable — tracks retain relative order). */
static void sort_events(midi_event_t *ev, uint32_t n)
{
	for (uint32_t i = 1; i < n; i++) {
		midi_event_t key = ev[i];
		int32_t j = (int32_t)i - 1;

		while (j >= 0 && ev[j].tick > key.tick) {
			ev[j + 1] = ev[j];
			j--;
		}
		ev[j + 1] = key;
	}
}

int midi_smf_parse(midi_song_t *song, const uint8_t *data, uint32_t len)
{
	if (!song || !data || len < 14) {
		return -1;
	}
	if (data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd') {
		return -1;
	}

	uint32_t hdr_len  = be32(data, 4);
	uint16_t format   = be16(data, 8);
	uint16_t n_tracks = be16(data, 10);
	uint16_t division = be16(data, 12);

	if (hdr_len < 6 || (format != 0 && format != 1) || (division & 0x8000)) {
		/* SMPTE time codes not supported */
		return -1;
	}

	song->ticks_per_beat = division;
	song->count          = 0;

	uint32_t p = 8 + hdr_len;  /* skip to first track */

	for (uint16_t t = 0; t < n_tracks && p + 8 <= len; t++) {
		if (data[p] != 'M' || data[p+1] != 'T' ||
		    data[p+2] != 'r' || data[p+3] != 'k') {
			return -1;
		}
		uint32_t tlen = be32(data, p + 4);
		uint32_t tstart = p + 8;
		uint32_t tend   = tstart + tlen;

		if (tend > len) {
			return -1;
		}
		if (parse_track(song->events, MIDI_MAX_EVENTS, &song->count,
				data, tstart, tend) < 0) {
			return -1;
		}
		p = tend;
	}

	if (format == 1 && n_tracks > 1) {
		sort_events(song->events, song->count);
	}

	return 0;
}
