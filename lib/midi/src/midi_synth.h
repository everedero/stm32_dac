/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "midi_smf.h"

/*
 * Polyphonic DDS sine synthesizer driven by a pre-parsed midi_song_t.
 *
 * midi_synth_init()  — call once from main() before starting DMA.
 * midi_synth_fill()  — call from the DMA TX-done callback to produce PCM.
 * midi_synth_restart() — reset to the beginning of the song (looping).
 */

int  midi_synth_init(const midi_song_t *song, uint32_t sample_rate);
bool midi_synth_fill(int16_t *buf, uint32_t n);   /* returns true at end */
void midi_synth_restart(void);
