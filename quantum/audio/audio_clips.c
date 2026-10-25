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
#include <stddef.h>
#include "audio_clips.h"
#include "audio.h"
#include "sync_timer.h"
#include "keyboard.h"
#if defined(SPLIT_KEYBOARD)
#    include "split_util.h"
#endif

// Weak references rather than weak definitions, which the compiler could fold to an empty table,
// so that the clips are found at run time, or are none if nothing defines them.
extern const audio_clip_t audio_clips[] __attribute__((weak));
extern const uint8_t      audio_clips_count __attribute__((weak));

static uint8_t clips_count(void) {
    return &audio_clips_count ? audio_clips_count : 0;
}

#if defined(SPLIT_KEYBOARD) && defined(SPLIT_AUDIO_ENABLE)
audio_clip_cue_t audio_clip_cue = {.clip = AUDIO_CLIP_STOP};

// The master's last cue has yet to reach the slave, since cue_since on the sync timer.
static bool     cue_pending = false;
static uint32_t cue_since   = 0;
#endif

static struct {
    const audio_clip_note_t *notes; // NULL when nothing plays
    uint8_t                  count;
    uint8_t                  tempo;
    uint32_t                 start; // on the sync timer
    uint8_t                  index; // the current note
    uint32_t                 ticks; // when the current note starts
    float                    pitch; // what sounds, or 0
} player;

static uint32_t ticks_to_ms(uint32_t ticks) {
    return ticks * 60000 / ((uint32_t)player.tempo * AUDIO_CLIP_TICKS_PER_BEAT);
}

static void hush(void) {
    if (player.pitch > 0.0f) {
        audio_stop_tone(player.pitch);
        player.pitch = 0.0f;
    }
}

// Plays one voice of a clip, on this half, from a time on the sync timer.
static void play_voice(uint8_t clip, audio_clip_side_t side, uint32_t start) {
    hush();
    player.notes = NULL;
    if (clip >= clips_count()) {
        return;
    }

    const audio_clip_t       *c     = &audio_clips[clip];
    const audio_clip_voice_t *voice = side == AUDIO_CLIP_LEFT ? &c->left : &c->right;
    if (!voice->count || !c->tempo) {
        return;
    }

    player.notes = voice->notes;
    player.count = voice->count;
    player.tempo = c->tempo;
    player.start = start;
    player.index = 0;
    player.ticks = 0;
}

static audio_clip_side_t own_side(uint8_t clip) {
#if defined(SPLIT_KEYBOARD) && defined(SPLIT_AUDIO_ENABLE)
    if (!is_keyboard_master() || is_transport_connected()) {
        return is_keyboard_left() ? AUDIO_CLIP_LEFT : AUDIO_CLIP_RIGHT;
    }
#endif
    return audio_clips[clip].solo;
}

void audio_clip_play(uint8_t clip) {
    uint32_t start = sync_timer_read32();
#if defined(SPLIT_KEYBOARD) && defined(SPLIT_AUDIO_ENABLE)
    if (is_keyboard_master()) {
        // The slave may have just started and not answered yet, which the transport counts as
        // connected, so the master's own voice waits for the cue to reach it.
        cue_pending = is_transport_connected() && clip < clips_count();
        cue_since   = start;
        start += AUDIO_CLIP_LEAD_MS;
        audio_clip_cue.start = start;
        audio_clip_cue.clip  = clip;
        audio_clip_cue.serial++;
    }
#endif
    if (clip >= clips_count()) {
        hush();
        player.notes = NULL;
        return;
    }
    play_voice(clip, own_side(clip), start);
}

void audio_clip_stop(void) {
    audio_clip_play(AUDIO_CLIP_STOP);
}

bool audio_clip_is_playing(void) {
    return player.notes != NULL;
}

#if defined(SPLIT_KEYBOARD) && defined(SPLIT_AUDIO_ENABLE)
void audio_clip_play_cue(const audio_clip_cue_t *cue) {
    if (cue->clip >= clips_count()) {
        hush();
        player.notes = NULL;
        return;
    }
    play_voice(cue->clip, is_keyboard_left() ? AUDIO_CLIP_LEFT : AUDIO_CLIP_RIGHT, cue->start);
}

void audio_clip_cue_delivered(uint8_t serial) {
    if (serial == audio_clip_cue.serial) {
        cue_pending = false;
    }
}

// Called when a cue falls due before the slave has it: cues the clip again a little later, or
// gives up on the slave, plays the solo voice, and calls the cue off so a late slave stays quiet.
static void cue_again(void) {
    uint32_t now   = sync_timer_read32();
    uint8_t  clip  = audio_clip_cue.clip;
    uint32_t start = now + AUDIO_CLIP_LEAD_MS;
    audio_clip_cue.serial++;
    if (now - cue_since >= AUDIO_CLIP_CONNECT_WAIT_MS) {
        cue_pending         = false;
        audio_clip_cue.clip = AUDIO_CLIP_STOP;
        play_voice(clip, audio_clips[clip].solo, now);
    } else {
        audio_clip_cue.start = start;
        player.start         = start;
    }
}
#endif

void audio_clips_task(void) {
    if (!player.notes) {
        return;
    }

    int32_t elapsed = (int32_t)(sync_timer_read32() - player.start);
    if (elapsed < 0) {
        return;
    }
#if defined(SPLIT_KEYBOARD) && defined(SPLIT_AUDIO_ENABLE)
    if (cue_pending) {
        cue_again();
        return;
    }
#endif

    // Past the notes that have ended, which a slow pass can skip whole.
    while (player.index < player.count && (uint32_t)elapsed >= ticks_to_ms(player.ticks + player.notes[player.index].ticks)) {
        hush();
        player.ticks += player.notes[player.index].ticks;
        player.index++;
    }
    if (player.index >= player.count) {
        hush();
        player.notes = NULL;
        return;
    }

    const audio_clip_note_t *note  = &player.notes[player.index];
    bool                     sound = note->pitch > 0.0f && (uint32_t)elapsed + AUDIO_CLIP_NOTE_GAP_MS < ticks_to_ms(player.ticks + note->ticks);
    if (sound && player.pitch == 0.0f) {
        if (!audio_is_on()) {
            return;
        }
        player.pitch = note->pitch;
        audio_play_tone(player.pitch);
    } else if (!sound) {
        hush();
    }
}
