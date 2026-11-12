#pragma once

// Audio output for frontends: one SDL audio stream on the default playback
// device. Takes interleaved stereo int16 at the core's sample rate; SDL
// resamples it once, straight to whatever rate the device runs at.
//
// Speed (audio_output_set_speed), following the frontend's timing mode
// (decided 2026-11-12):
//   - Exact: the core runs at its own fps, so its audio plays at its own rate
//     (ratio 1). Only the clocks' tiny drift is left, handled as below.
//   - Vsync: the core runs at the display rate, so its audio must play
//     display Hz / core fps faster or slower, plus dynamic rate control (DRC):
//     a correction of at most AUDIO_OUTPUT_DRC_MAX_DEVIATION that keeps the
//     queue near AUDIO_OUTPUT_DRC_TARGET_MS. Both through SDL's own resampler
//     (SDL_SetAudioStreamFrequencyRatio); an own resampler can replace it here later.
//
// Safety nets for whatever is left (stalls, a hidden window):
//   - too much queued (above AUDIO_OUTPUT_MAX_QUEUED_MS): drop the new audio;
//   - queue ran dry: count an underrun and start again from a little silence
//     (AUDIO_OUTPUT_START_SILENCE_MS), so the next frames don't underrun at once.
//
// The first AUDIO_OUTPUT_WARMUP_PUSHES pushes after create/clear refill a dry
// queue the same way but don't count it as an underrun: a core's first frames
// are slow (pipelines, textures), so a gap there says nothing about pacing.
//
// Usage:
//   audio_output_create(&out, sample_rate)    when a core starts
//   audio_output_set_speed(&out, ratio, drc)  when the timing mode or fps changes
//   audio_output_push(&out, samples, frames)  after every retro_run
//   audio_output_destroy(&out)                when it stops (safe if create failed)

#include <cstddef>
#include <cstdint>

struct SDL_AudioStream;

constexpr uint32_t AUDIO_OUTPUT_MAX_QUEUED_MS    = 150;
constexpr uint32_t AUDIO_OUTPUT_START_SILENCE_MS = 50;
constexpr uint32_t AUDIO_OUTPUT_WARMUP_PUSHES    = 30;   // ~0.5 s at 60 fps

// Dynamic rate control (Vsync timing mode): the playback speed follows the
// queue, so it settles around AUDIO_OUTPUT_DRC_TARGET_MS instead of slowly
// filling up or running dry. At most this much faster or slower than the base
// speed: 0.5% is a pitch change nobody hears.
constexpr uint32_t AUDIO_OUTPUT_DRC_TARGET_MS     = 60;
constexpr double   AUDIO_OUTPUT_DRC_MAX_DEVIATION = 0.005;

struct AudioOutput
{
    SDL_AudioStream* stream          = nullptr;
    bool             subsystem_ready = false;   // we called SDL_InitSubSystem(SDL_INIT_AUDIO)
    int32_t          sample_rate     = 0;       // what the stream expects from us (Hz, rounded)
    bool             started         = false;   // something was pushed since create/clear
    uint32_t         pushes          = 0;       // pushes since create/clear (for the warm-up)

    // Playback speed: base_ratio x (DRC correction when rate_control is on).
    // SDL resamples by this ratio on top of the core rate -> device rate conversion.
    double           base_ratio      = 1.0;
    bool             rate_control    = false;
    double           current_ratio   = 1.0;     // what's set on the stream now (for debug UIs)

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

// base_ratio: how much faster than its own sample rate the core's audio must
// play. In Vsync timing mode the core runs at the display rate instead of its
// own fps, so base_ratio = display Hz / core fps (e.g. 59.91 / 60.0988); in
// Exact mode it's 1. rate_control: DRC on top of it (Vsync mode).
void audio_output_set_speed(AudioOutput* output, double base_ratio, bool rate_control);

// Queues `frames` stereo frames (frames * 2 int16 values).
void audio_output_push(AudioOutput* output, const int16_t* samples, size_t frames);

// Throws away everything queued (e.g. on pause, so resuming doesn't play stale audio).
void audio_output_clear(AudioOutput* output);

// Audio queued in our stream and not yet handed to the device, in milliseconds.
uint32_t audio_output_queued_ms(const AudioOutput* output);
