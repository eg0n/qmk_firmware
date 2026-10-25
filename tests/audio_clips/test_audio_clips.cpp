// Copyright 2026 Adam Brown
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "gtest/gtest.h"
#include "test_common.hpp"

extern "C" {
#include "audio_clips.h"

void advance_time(uint32_t ms);

// ARRAY_SIZE from util.h is for C only.
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

// At 125 bpm and 24 ticks to a beat, a tick is 20 ms.
static const audio_clip_note_t left_voice[]  = {AUDIO_CLIP_NOTE(_C5, 10), AUDIO_CLIP_NOTE(_REST, 5), AUDIO_CLIP_NOTE(_E5, 5)};
static const audio_clip_note_t right_voice[] = {AUDIO_CLIP_NOTE(_G5, 10), AUDIO_CLIP_NOTE(_A5, 10)};

extern const audio_clip_t audio_clips[];
const audio_clip_t        audio_clips[] = {
    {{left_voice, COUNT(left_voice)}, {right_voice, COUNT(right_voice)}, AUDIO_CLIP_RIGHT, 125},
};
extern const uint8_t audio_clips_count;
const uint8_t        audio_clips_count = COUNT(audio_clips);
}

namespace {

class AudioClipsTest : public TestFixture {
   public:
    // The keyboard task needs a host driver; these tests care for no reports.
    ::testing::NiceMock<TestDriver> driver;

    void SetUp() override {
        audio_clip_stop();
        audio_on();
        audio_stop_all();
    }

    // Starts the clip, and returns when it started.
    uint32_t play(uint8_t clip) {
        audio_clip_play(clip);
        return timer_read32();
    }

    // Runs the keyboard until it has run at ms after start.
    void run_to(uint32_t start, uint32_t ms) {
        while (timer_read32() - start <= ms) {
            run_one_scan_loop();
        }
    }

    float sounding() {
        return audio_is_playing_note() ? audio_get_frequency(0) : 0.0f;
    }
};

// A keyboard that is not split plays the solo voice, each note on time and with a gap at its end.
TEST_F(AudioClipsTest, PlaysTheSoloVoiceOnTime) {
    uint32_t start = play(0);
    EXPECT_TRUE(audio_clip_is_playing());

    run_to(start, 0);
    EXPECT_FLOAT_EQ(sounding(), NOTE_G5);
    run_to(start, 200 - AUDIO_CLIP_NOTE_GAP_MS - 1);
    EXPECT_FLOAT_EQ(sounding(), NOTE_G5);
    run_to(start, 200 - AUDIO_CLIP_NOTE_GAP_MS);
    EXPECT_FLOAT_EQ(sounding(), 0.0f);
    run_to(start, 200);
    EXPECT_FLOAT_EQ(sounding(), NOTE_A5);
    run_to(start, 400);
    EXPECT_FLOAT_EQ(sounding(), 0.0f);
    EXPECT_FALSE(audio_clip_is_playing());
}

// A pass that comes late skips the notes that have ended, rather than playing the clip late.
TEST_F(AudioClipsTest, KeepsTimeThroughASlowPass) {
    uint32_t start = play(0);
    run_to(start, 0);
    advance_time(250);
    run_one_scan_loop();
    EXPECT_FLOAT_EQ(sounding(), NOTE_A5);
}

TEST_F(AudioClipsTest, Stops) {
    uint32_t start = play(0);
    run_to(start, 50);
    audio_clip_stop();
    EXPECT_FALSE(audio_clip_is_playing());
    EXPECT_FLOAT_EQ(sounding(), 0.0f);
}

TEST_F(AudioClipsTest, IgnoresAnUnknownClip) {
    uint32_t start = play(7);
    EXPECT_FALSE(audio_clip_is_playing());
    run_to(start, 50);
    EXPECT_FLOAT_EQ(sounding(), 0.0f);
}

// With audio off the clip keeps time, but stays silent.
TEST_F(AudioClipsTest, StaysQuietWithAudioOff) {
    audio_off();
    uint32_t start = play(0);
    run_to(start, 50);
    EXPECT_TRUE(audio_clip_is_playing());
    EXPECT_FLOAT_EQ(sounding(), 0.0f);
    audio_on();
}

} // namespace
