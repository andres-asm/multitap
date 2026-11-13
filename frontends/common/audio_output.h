#pragma once

// Audio output for frontends: one SDL audio stream on the default playback
// device. Takes interleaved stereo int16 at the core's sample rate.
//
// The path (decided 2026-11-13, plan-milestones milestone 2):
//   core int16 -> float -> [DSP stage: empty for now; EQ / filters later]
//   -> libsamplerate (sinc, medium quality) at core rate x speed -> device rate
//   -> SDL stream, which only converts the sample format (same rate both sides).
// libsamplerate was chosen by measurement over SDL3's own resampler, speexdsp
// and libretro-common's sinc (tools/resampler_measure.cpp): clean high
// frequencies, strong alias/image rejection, and built for a ratio that
// changes all the time, which is what rate control does.
//
// Speed (audio_output_set_speed), following the frontend's timing mode
// (decided 2026-11-12):
//   - Exact: the core runs at its own fps, so its audio plays at its own rate
//     (base ratio 1).
//   - Vsync: the core runs at the display rate, so its audio must play
//     display Hz / core fps faster or slower (base ratio).
// Both modes add dynamic rate control (DRC) on top (rate_control.h: a PI
// controller, at most 0.5% faster or slower) that keeps the queue at the
// latency target. In Exact mode it absorbs the drift between the frame
// timer's clock and the audio device's clock (tens of ppm). The speed goes
// into libsamplerate's ratio.
//
// Latency target (audio_output_set_latency): fixed, or automatic (default):
// starts at RATE_CONTROL_AUTO_START_MS and finds the smallest target this
// machine survives (raised after an underrun, lowered after long quiet
// stretches with room to spare). Never below two of the device's own buffers
// + 10 ms: the device takes whole buffers from our queue.
//
// Safety nets for whatever is left (stalls, a hidden window):
//   - too much queued (above twice the target, at least AUDIO_OUTPUT_MAX_QUEUED_MS):
//     drop the new audio;
//   - queue ran dry: count an underrun (rate control raises an automatic
//     target). No silence is added then: the device may still be playing the
//     buffer it took, and silence would be a gap of our own. Only the very
//     first push starts from a cushion of AUDIO_OUTPUT_START_SILENCE_MS.
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
#include <vector>

#include "rate_control.h"

struct SDL_AudioStream;
struct SRC_STATE_tag;

constexpr uint32_t AUDIO_OUTPUT_MAX_QUEUED_MS    = 150;
constexpr uint32_t AUDIO_OUTPUT_START_SILENCE_MS = 50;
constexpr uint32_t AUDIO_OUTPUT_WARMUP_PUSHES    = 30;   // ~0.5 s at 60 fps


struct AudioOutput
{
    SDL_AudioStream* stream          = nullptr;
    SRC_STATE_tag*   resampler       = nullptr;   // libsamplerate
    bool             subsystem_ready = false;     // we called SDL_InitSubSystem(SDL_INIT_AUDIO)
    double           core_rate       = 0.0;       // what the core gives us (Hz, may be fractional)
    int32_t          device_rate     = 0;         // what the stream (and device) takes (Hz)
    int32_t          sample_rate     = 0;         // core_rate rounded, for debug UIs
    bool             started         = false;     // something was pushed since create/clear
    uint32_t         pushes          = 0;         // pushes since create/clear (for the warm-up)

    // Playback speed: base_ratio x (DRC correction when rate_control is on).
    double           base_ratio      = 1.0;
    bool             rate_control    = false;
    double           current_ratio   = 1.0;       // the speed in use now (for debug UIs)
    RateControl      rate            = {};        // DRC + latency target (rate.target_ms)
    uint32_t         device_buffer_ms = 0;        // the device's own buffer (floor = 2 x this + 10)
    uint64_t         last_push_ns    = 0;

    // Scratch buffers for the float path, sized at create; they only grow if
    // a core pushes a bigger block than expected (not every frame).
    std::vector<float> input;
    std::vector<float> output;

    // Statistics for debug UIs.
    uint64_t underruns      = 0;   // times the queue ran dry while playing
    uint64_t dropped_frames = 0;   // stereo frames thrown away because the queue was full
};

// sample_rate is the core's (it may be fractional, e.g. 32040.5 Hz).
// Returns false (and logs why) if no audio device could be opened; the frontend
// can keep running without sound.
bool audio_output_create(AudioOutput* output, double sample_rate);
void audio_output_destroy(AudioOutput* output);

// Changes the input sample rate (a core may call SET_SYSTEM_AV_INFO). Cheap; does nothing if unchanged.
void audio_output_set_sample_rate(AudioOutput* output, double sample_rate);

// base_ratio: how much faster than its own sample rate the core's audio must
// play. In Vsync timing mode the core runs at the display rate instead of its
// own fps, so base_ratio = display Hz / core fps (e.g. 59.91 / 60.0988); in
// Exact mode it's 1. rate_control: DRC on top of it (frontends use it in both modes).
void audio_output_set_speed(AudioOutput* output, double base_ratio, bool rate_control);

// The latency target: fixed at `target_ms`, or automatic starting from it
// (e.g. what was learned last time). Clamped to the floor and
// RATE_CONTROL_MAX_TARGET_MS. Takes effect at once.
void audio_output_set_latency(AudioOutput* output, uint32_t target_ms, bool automatic);

// Queues `frames` stereo frames (frames * 2 int16 values).
void audio_output_push(AudioOutput* output, const int16_t* samples, size_t frames);

// Throws away everything queued (e.g. on pause, so resuming doesn't play stale audio).
void audio_output_clear(AudioOutput* output);

// Audio queued in our stream and not yet handed to the device, in milliseconds.
uint32_t audio_output_queued_ms(const AudioOutput* output);
