/* Copyright 2026 Adam Brown
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "util.h"

/*
 * Audio clips: short tunes with one voice for each half of a split keyboard.
 *
 * Every half holds the same table of clips, and plays its own voice of one when asked. On a
 * split keyboard with SPLIT_AUDIO_ENABLE, the master cues the slave over the split transport
 * with the clip's index and a start time on the shared sync timer, so that the two voices play
 * together, in stereo. A half playing alone - the only half, or a master with no slave - plays
 * the clip's solo voice.
 *
 * Notes are timed in ticks from the clip's start, AUDIO_CLIP_TICKS_PER_BEAT to a beat at the
 * clip's tempo, rather than one after another, so that neither voice drifts.
 */

#ifndef AUDIO_CLIP_TICKS_PER_BEAT
// 24 ticks to a beat hold sixteenths (6), thirty-seconds (3), dotted notes and triplets (4)
#    define AUDIO_CLIP_TICKS_PER_BEAT 24
#endif

#ifndef AUDIO_CLIP_NOTE_GAP_MS
// The silence at the end of each note, so that repeated notes are heard apart. The audio
// driver also starts each note from silence, which some drivers do with a fixed delay, so that
// both halves keep the same timing.
#    define AUDIO_CLIP_NOTE_GAP_MS 12
#endif

#ifndef AUDIO_CLIP_LEAD_MS
// How far ahead the master cues a clip, for the cue to reach the slave before it starts.
#    define AUDIO_CLIP_LEAD_MS 50
#endif

#ifndef AUDIO_CLIP_CONNECT_WAIT_MS
// How long the master holds a clip back for the slave to take its cue, as when both halves have
// just started and the slave is still finding out that it is one, before playing it alone.
#    define AUDIO_CLIP_CONNECT_WAIT_MS 3000
#endif

#define AUDIO_CLIP_STOP 0xFF

typedef struct audio_clip_note_t {
    float   pitch; // in Hz, from musical_notes.h; 0 for a rest
    uint8_t ticks;
} audio_clip_note_t;

typedef struct audio_clip_voice_t {
    const audio_clip_note_t *notes;
    uint8_t                  count;
} audio_clip_voice_t;

typedef enum audio_clip_side_t { AUDIO_CLIP_LEFT, AUDIO_CLIP_RIGHT } audio_clip_side_t;

typedef struct audio_clip_t {
    audio_clip_voice_t left;
    audio_clip_voice_t right;
    audio_clip_side_t  solo;  // the voice a half plays alone
    uint8_t            tempo; // in beats per minute
} audio_clip_t;

// A voice from an array of notes, for the left or right of an audio_clip_t.
#define AUDIO_CLIP_VOICE(note_array) {.notes = (note_array), .count = ARRAY_SIZE(note_array)}
// One note, or a rest with pitch 0, from the NOTE_ values of musical_notes.h.
#define AUDIO_CLIP_NOTE(note, ticks) {(NOTE##note), (ticks)}

/*
 * The clips, which the keymap or keyboard defines, the same on both halves:
 *
 *   const audio_clip_t audio_clips[] = {...};
 *   const uint8_t      audio_clips_count = ARRAY_SIZE(audio_clips);
 */
extern const audio_clip_t audio_clips[];
extern const uint8_t      audio_clips_count;

// A cue for a clip, which the master sends the slave.
typedef struct audio_clip_cue_t {
    uint32_t start;  // on the sync timer
    uint8_t  clip;   // an index into audio_clips, or AUDIO_CLIP_STOP
    uint8_t  serial; // changes with every cue
} audio_clip_cue_t;

/*
 * Plays a clip from the start, or stops one with AUDIO_CLIP_STOP. On the master of a split
 * keyboard with SPLIT_AUDIO_ENABLE, this plays both voices, one on each half.
 */
void audio_clip_play(uint8_t clip);
void audio_clip_stop(void);
bool audio_clip_is_playing(void);

// Called from the keyboard task on every half, to start and stop the notes on time.
void audio_clips_task(void);

#if defined(SPLIT_KEYBOARD) && defined(SPLIT_AUDIO_ENABLE)
// The last cue, which the split transport sends the slave.
extern audio_clip_cue_t audio_clip_cue;

// Plays a cue from the master, on the slave.
void audio_clip_play_cue(const audio_clip_cue_t *cue);

// Tells the master that the slave has a cue, so that the master starts its own voice of it.
void audio_clip_cue_delivered(uint8_t serial);
#endif
