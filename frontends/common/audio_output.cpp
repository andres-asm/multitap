#include "audio_output.h"

#include <SDL3/SDL.h>

static constexpr int32_t CHANNELS        = 2;
static constexpr int32_t BYTES_PER_FRAME = CHANNELS * static_cast<int32_t>(sizeof(int16_t));

// Anything outside this is a broken core, not a real sample rate.
static constexpr double MIN_SAMPLE_RATE = 1000.0;
static constexpr double MAX_SAMPLE_RATE = 384000.0;

static bool rate_to_hz(double sample_rate, int32_t* hz)
{
    if (!(sample_rate >= MIN_SAMPLE_RATE && sample_rate <= MAX_SAMPLE_RATE))
    {
        return false;
    }
    // SDL takes whole Hz. Rounding e.g. 32040.5 to 32041 is a 0.0016% speed
    // error, far below what anyone can hear and below the vsync drift we already accept.
    *hz = static_cast<int32_t>(sample_rate + 0.5);
    return true;
}

static uint32_t frames_to_ms(int64_t frames, int32_t sample_rate)
{
    if (sample_rate <= 0 || frames <= 0)
    {
        return 0;
    }
    return static_cast<uint32_t>(frames * 1000 / sample_rate);
}

static int64_t queued_frames(const AudioOutput* output)
{
    int queued_bytes = SDL_GetAudioStreamQueued(output->stream);
    if (queued_bytes < 0)
    {
        return 0;
    }
    return queued_bytes / BYTES_PER_FRAME;
}

static void push_silence(AudioOutput* output, uint32_t milliseconds)
{
    static const int16_t ZEROS[1024 * CHANNELS] = {};
    constexpr int64_t ZERO_FRAMES = 1024;

    int64_t remaining = static_cast<int64_t>(output->sample_rate) * milliseconds / 1000;
    while (remaining > 0)
    {
        int64_t chunk = remaining < ZERO_FRAMES ? remaining : ZERO_FRAMES;
        SDL_PutAudioStreamData(output->stream, ZEROS, static_cast<int>(chunk * BYTES_PER_FRAME));
        remaining -= chunk;
    }
}

bool audio_output_create(AudioOutput* output, double sample_rate)
{
    *output = AudioOutput{};

    int32_t hz = 0;
    if (!rate_to_hz(sample_rate, &hz))
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio: the core's sample rate (%.1f Hz) is not usable; no sound", sample_rate);
        return false;
    }

    // Audio is initialized here rather than in app_shell: frontends without
    // sound (piccolo today) never open an audio device.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio: SDL_InitSubSystem failed: %s", SDL_GetError());
        return false;
    }
    output->subsystem_ready = true;

    // The spec describes what WE put in. SDL converts and resamples it to the
    // device's own format and rate inside the stream.
    SDL_AudioSpec spec = {
        .format   = SDL_AUDIO_S16,
        .channels = CHANNELS,
        .freq     = hz,
    };
    output->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (output->stream == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio: could not open the playback device: %s", SDL_GetError());
        audio_output_destroy(output);
        return false;
    }
    output->sample_rate = hz;

    // Device streams start paused.
    if (!SDL_ResumeAudioStreamDevice(output->stream))
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio: could not start the playback device: %s", SDL_GetError());
        audio_output_destroy(output);
        return false;
    }

    SDL_AudioSpec device_spec = {};
    int device_buffer_frames = 0;
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(output->stream), &device_spec, &device_buffer_frames);
    SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, "Audio: %s, core %d Hz -> device %d Hz, %d ch, %d-frame device buffer",
                SDL_GetCurrentAudioDriver(), hz, device_spec.freq, device_spec.channels, device_buffer_frames);
    return true;
}

void audio_output_destroy(AudioOutput* output)
{
    if (output->stream != nullptr)
    {
        // Also closes the device SDL_OpenAudioDeviceStream opened for it.
        SDL_DestroyAudioStream(output->stream);
    }
    if (output->subsystem_ready)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    *output = AudioOutput{};
}

void audio_output_set_sample_rate(AudioOutput* output, double sample_rate)
{
    int32_t hz = 0;
    if (output->stream == nullptr || !rate_to_hz(sample_rate, &hz) || hz == output->sample_rate)
    {
        return;
    }

    SDL_AudioSpec spec = {
        .format   = SDL_AUDIO_S16,
        .channels = CHANNELS,
        .freq     = hz,
    };
    // Only the input side changes; the device side (nullptr) stays as it is.
    if (!SDL_SetAudioStreamFormat(output->stream, &spec, nullptr))
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio: could not switch to %d Hz: %s", hz, SDL_GetError());
        return;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, "Audio: core sample rate changed %d -> %d Hz", output->sample_rate, hz);
    output->sample_rate = hz;
}

static void apply_ratio(AudioOutput* output, double ratio)
{
    // SDL accepts 0.01..100; anything far from 1 here would be a bug upstream.
    if (ratio < 0.5 || ratio > 2.0 || ratio == output->current_ratio)
    {
        return;
    }
    if (SDL_SetAudioStreamFrequencyRatio(output->stream, static_cast<float>(ratio)))
    {
        output->current_ratio = ratio;
    }
}

void audio_output_set_speed(AudioOutput* output, double base_ratio, bool rate_control)
{
    if (output->stream == nullptr)
    {
        return;
    }
    output->base_ratio = base_ratio;
    output->rate_control = rate_control;
    if (!rate_control)
    {
        apply_ratio(output, base_ratio);
    }
}

// DRC: more queued than the target -> play a little faster, less -> slower.
// Proportional, so the queue settles near the target without oscillating.
static void update_rate_control(AudioOutput* output, int64_t queued_frames_now)
{
    if (!output->rate_control)
    {
        return;
    }
    double target = static_cast<double>(output->sample_rate) * AUDIO_OUTPUT_DRC_TARGET_MS / 1000.0;
    double deviation = (static_cast<double>(queued_frames_now) - target) / target;
    deviation = deviation < -1.0 ? -1.0 : (deviation > 1.0 ? 1.0 : deviation);
    apply_ratio(output, output->base_ratio * (1.0 + AUDIO_OUTPUT_DRC_MAX_DEVIATION * deviation));
}

void audio_output_push(AudioOutput* output, const int16_t* samples, size_t frames)
{
    if (output->stream == nullptr || samples == nullptr || frames == 0)
    {
        return;
    }

    int64_t queued = queued_frames(output);

    // Ran dry: the device played everything we had and is now playing silence
    // (an audible gap). Start again with a cushion so we don't hit it next frame.
    // During the warm-up (slow first frames of a core) the refill happens but isn't counted.
    bool warming_up = output->pushes < AUDIO_OUTPUT_WARMUP_PUSHES;
    if (output->pushes < UINT32_MAX)
    {
        output->pushes++;
    }
    if (queued == 0)
    {
        if (output->started && !warming_up)
        {
            output->underruns++;
            // Logged one by one: the timing pattern tells drift (evenly spaced)
            // from stalls (bursts, e.g. the window was hidden and vsync stopped).
            SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, "Audio underrun #%llu at %.2f s",
                        static_cast<unsigned long long>(output->underruns),
                        static_cast<double>(SDL_GetTicks()) / 1000.0);
        }
        push_silence(output, AUDIO_OUTPUT_START_SILENCE_MS);
        queued = queued_frames(output);
    }
    output->started = true;
    update_rate_control(output, queued);

    // Too far ahead (display faster than the core, or a burst of frames):
    // dropping this block keeps latency bounded at the cost of a click (see the
    // header for how often that happens away from the core's fps).
    if (frames_to_ms(queued, output->sample_rate) > AUDIO_OUTPUT_MAX_QUEUED_MS)
    {
        output->dropped_frames += frames;
        return;
    }

    SDL_PutAudioStreamData(output->stream, samples, static_cast<int>(frames * static_cast<size_t>(BYTES_PER_FRAME)));
}

void audio_output_clear(AudioOutput* output)
{
    if (output->stream == nullptr)
    {
        return;
    }
    SDL_ClearAudioStream(output->stream);
    // Not an underrun when we start pushing again: the gap was on purpose.
    output->started = false;
    output->pushes = 0;
}

uint32_t audio_output_queued_ms(const AudioOutput* output)
{
    if (output->stream == nullptr)
    {
        return 0;
    }
    return frames_to_ms(queued_frames(output), output->sample_rate);
}
