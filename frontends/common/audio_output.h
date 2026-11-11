#pragma once

// Basic audio output for frontends: one SDL audio stream on the default
// playback device. Takes interleaved stereo int16 at the core's sample rate;
// SDL resamples it to whatever the device runs at.
//
// Deliberately basic (decided 2026-11-11): no dynamic rate control. The core is
// paced by vsync, so on a display that isn't exactly the core's fps the queue
// slowly fills or drains. We handle both ends simply:
//   - too much queued (above AUDIO_OUTPUT_MAX_QUEUED_MS): drop the new audio;
//   - queue ran dry: count an underrun and start again from a little silence
//     (AUDIO_OUTPUT_START_SILENCE_MS), so the next frames don't underrun at once.
// Audio sync options proper are piccolo's job (milestone 2).
//
// Usage:
//   audio_output_create(&out, sample_rate)    when a core starts
//   audio_output_push(&out, samples, frames)  after every retro_run
//   audio_output_destroy(&out)                when it stops (safe if create failed)

#include <cstddef>
#include <cstdint>

struct SDL_AudioStream;

constexpr uint32_t AUDIO_OUTPUT_MAX_QUEUED_MS    = 150;
constexpr uint32_t AUDIO_OUTPUT_START_SILENCE_MS = 50;

struct AudioOutput
{
    SDL_AudioStream* stream          = nullptr;
    bool             subsystem_ready = false;   // we called SDL_InitSubSystem(SDL_INIT_AUDIO)
    int32_t          sample_rate     = 0;       // what the stream expects from us (Hz, rounded)
    bool             started         = false;   // something was pushed since create/clear

    // Statistics for debug UIs.
    uint64_t underruns      = 0;   // times the queue ran dry while playing
    uint64_t dropped_frames = 0;   // stereo frames thrown away because the queue was full
};

// sample_rate is the core's (it may be fractional, e.g. 32040.5 Hz; we round it).
// Returns false (and logs why) if no audio device could be opened; the frontend
// can keep running without sound.
bool audio_output_create(AudioOutput* output, double sample_rate);
void audio_output_destroy(AudioOutput* output);

// Changes the input sample rate (a core may call SET_SYSTEM_AV_INFO). Cheap; does nothing if unchanged.
void audio_output_set_sample_rate(AudioOutput* output, double sample_rate);

// Queues `frames` stereo frames (frames * 2 int16 values).
void audio_output_push(AudioOutput* output, const int16_t* samples, size_t frames);

// Throws away everything queued (e.g. on pause, so resuming doesn't play stale audio).
void audio_output_clear(AudioOutput* output);

// Audio queued in our stream and not yet handed to the device, in milliseconds.
uint32_t audio_output_queued_ms(const AudioOutput* output);
