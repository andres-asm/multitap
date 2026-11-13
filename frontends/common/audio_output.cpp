#include "audio_output.h"

#include <SDL3/SDL.h>

#include "samplerate.h"

static constexpr int32_t CHANNELS        = 2;
static constexpr int32_t BYTES_PER_FRAME = CHANNELS * static_cast<int32_t>(sizeof(float));   // what the stream takes

// Anything outside this is a broken core, not a real sample rate.
static constexpr double MIN_SAMPLE_RATE = 1000.0;
static constexpr double MAX_SAMPLE_RATE = 384000.0;

// Scratch room at create: a core's frame is usually under 1000 frames
// (48 kHz / 50 fps = 960); more only grows the buffers once.
static constexpr size_t INITIAL_SCRATCH_FRAMES = 4096;

// libsamplerate's ratio = output rate / input rate; it refuses outside this.
static constexpr double MIN_RESAMPLE_RATIO = 1.0 / 256.0;
static constexpr double MAX_RESAMPLE_RATIO = 256.0;

static bool sample_rate_usable(double sample_rate)
{
    return sample_rate >= MIN_SAMPLE_RATE && sample_rate <= MAX_SAMPLE_RATE;
}

static uint32_t frames_to_ms(int64_t frames, int32_t rate)
{
    if (rate <= 0 || frames <= 0)
    {
        return 0;
    }
    return static_cast<uint32_t>(frames * 1000 / rate);
}

// Frames (at the device rate) queued in our stream.
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
    static const float ZEROS[1024 * CHANNELS] = {};
    constexpr int64_t ZERO_FRAMES = 1024;

    int64_t remaining = static_cast<int64_t>(output->device_rate) * milliseconds / 1000;
    while (remaining > 0)
    {
        int64_t chunk = remaining < ZERO_FRAMES ? remaining : ZERO_FRAMES;
        SDL_PutAudioStreamData(output->stream, ZEROS, static_cast<int>(chunk * BYTES_PER_FRAME));
        remaining -= chunk;
    }
}

// What libsamplerate is asked for: device frames per core frame, slowed or
// sped up by the playback speed.
static double resample_ratio(const AudioOutput* output)
{
    double ratio = static_cast<double>(output->device_rate) / (output->core_rate * output->current_ratio);
    return ratio < MIN_RESAMPLE_RATIO ? MIN_RESAMPLE_RATIO : (ratio > MAX_RESAMPLE_RATIO ? MAX_RESAMPLE_RATIO : ratio);
}

bool audio_output_create(AudioOutput* output, double sample_rate)
{
    *output = AudioOutput{};
    if (!sample_rate_usable(sample_rate))
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio: the core's sample rate (%.1f Hz) is not usable; no sound", sample_rate);
        return false;
    }

    // Audio is initialized here rather than in app_shell: a frontend that
    // never plays sound never opens an audio device.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio: SDL_InitSubSystem failed: %s", SDL_GetError());
        return false;
    }
    output->subsystem_ready = true;

    // We resample to the device's own rate, so SDL doesn't resample again.
    SDL_AudioSpec device_spec = {};
    int device_buffer_frames = 0;
    if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device_spec, &device_buffer_frames) ||
        device_spec.freq <= 0)
    {
        device_spec.freq = 48000;   // a guess SDL will convert from if the device differs
    }
    SDL_AudioSpec spec = {
        .format   = SDL_AUDIO_F32,
        .channels = CHANNELS,
        .freq     = device_spec.freq,
    };
    output->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (output->stream == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio: could not open the playback device: %s", SDL_GetError());
        audio_output_destroy(output);
        return false;
    }
    output->device_rate = device_spec.freq;
    output->core_rate = sample_rate;
    output->sample_rate = static_cast<int32_t>(sample_rate + 0.5);

    int error = 0;
    output->resampler = src_new(SRC_SINC_MEDIUM_QUALITY, CHANNELS, &error);
    if (output->resampler == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio: could not create the resampler: %s", src_strerror(error));
        audio_output_destroy(output);
        return false;
    }
    output->input.resize(INITIAL_SCRATCH_FRAMES * CHANNELS);
    output->output.resize(INITIAL_SCRATCH_FRAMES * CHANNELS * 2);

    // Device streams start paused.
    if (!SDL_ResumeAudioStreamDevice(output->stream))
    {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio: could not start the playback device: %s", SDL_GetError());
        audio_output_destroy(output);
        return false;
    }

    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(output->stream), &device_spec, &device_buffer_frames);
    output->device_buffer_ms = frames_to_ms(device_buffer_frames, output->device_rate);
    audio_output_set_latency(output, RATE_CONTROL_AUTO_START_MS, true);
    SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO,
                "Audio: %s, core %.1f Hz -> device %d Hz (libsamplerate, sinc medium), %d ch, %d-frame device buffer",
                SDL_GetCurrentAudioDriver(), sample_rate, output->device_rate, device_spec.channels,
                device_buffer_frames);
    return true;
}

void audio_output_destroy(AudioOutput* output)
{
    if (output->stream != nullptr)
    {
        // Also closes the device SDL_OpenAudioDeviceStream opened for it.
        SDL_DestroyAudioStream(output->stream);
    }
    if (output->resampler != nullptr)
    {
        src_delete(output->resampler);
    }
    if (output->subsystem_ready)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    *output = AudioOutput{};
}

void audio_output_set_sample_rate(AudioOutput* output, double sample_rate)
{
    if (output->stream == nullptr || !sample_rate_usable(sample_rate) || sample_rate == output->core_rate)
    {
        return;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, "Audio: core sample rate changed %.1f -> %.1f Hz", output->core_rate,
                sample_rate);
    output->core_rate = sample_rate;
    output->sample_rate = static_cast<int32_t>(sample_rate + 0.5);
    // A jump, not a glide: the new rate applies from the next block on.
    src_set_ratio(output->resampler, resample_ratio(output));
}

void audio_output_set_speed(AudioOutput* output, double base_ratio, bool rate_control)
{
    if (output->stream == nullptr)
    {
        return;
    }
    // A different base speed (e.g. the timing mode changed) makes the learned
    // correction wrong: start learning again.
    double change = base_ratio / output->base_ratio - 1.0;
    if (change > 0.001 || change < -0.001)
    {
        rate_control_reset(&output->rate);
    }
    output->base_ratio = base_ratio;
    output->rate_control = rate_control;
    if (!rate_control)
    {
        output->current_ratio = base_ratio;
    }
}

void audio_output_set_latency(AudioOutput* output, uint32_t target_ms, bool automatic)
{
    // The device takes whole buffers from our queue: below two of them it
    // would empty the queue on every request.
    rate_control_init(&output->rate, target_ms, automatic, output->device_buffer_ms * 2 + 10);
}

// DRC (rate_control.h): more queued than the target -> play a little faster,
// less -> slower. libsamplerate glides to the new ratio within the next
// block (no step).
static void update_rate_control(AudioOutput* output, int64_t queued_frames_now, bool underrun)
{
    uint64_t now = SDL_GetTicksNS();
    double elapsed = output->last_push_ns > 0 ? static_cast<double>(now - output->last_push_ns) / 1e9 : 0.0;
    output->last_push_ns = now;
    if (!output->rate_control)
    {
        return;
    }
    double queued_ms = static_cast<double>(queued_frames_now) * 1000.0 / static_cast<double>(output->device_rate);
    double target_before = output->rate.target_ms;
    output->current_ratio = output->base_ratio * rate_control_update(&output->rate, queued_ms, elapsed, underrun);
    if (output->rate.target_ms != target_before)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, "Audio latency target: %.0f -> %.0f ms (automatic)", target_before,
                    output->rate.target_ms);
    }
}

// The DSP stage: where an equalizer and filters will go (plan-milestones,
// milestone 2, "future"). It works on the core's audio as float, before
// resampling. Empty for now.
static void apply_dsp(AudioOutput* output, float* samples, size_t frames)
{
    (void)output;
    (void)samples;
    (void)frames;
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
    bool underrun = false;
    if (!output->started)
    {
        // A cushion to start from, so the first frames don't run dry at once.
        push_silence(output, AUDIO_OUTPUT_START_SILENCE_MS);
        queued = queued_frames(output);
    }
    else if (queued == 0 && !warming_up)
    {
        // Our queue ran dry. The device may still be playing what it took last
        // (it takes whole buffers), so this isn't always audible yet; no
        // silence is added (that would make a gap of our own). It does mean the
        // target is too low for this device or the loop stalled: rate control
        // raises an automatic target.
        underrun = true;
        output->underruns++;
        // Logged one by one: the timing pattern tells drift (evenly spaced)
        // from stalls (bursts, e.g. the window was hidden and vsync stopped).
        SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, "Audio underrun #%llu at %.2f s",
                    static_cast<unsigned long long>(output->underruns),
                    static_cast<double>(SDL_GetTicks()) / 1000.0);
    }
    output->started = true;
    update_rate_control(output, queued, underrun);

    // Too far ahead (display faster than the core, or a burst of frames):
    // dropping this block keeps latency bounded at the cost of a click.
    double limit_ms = 2.0 * output->rate.target_ms;
    limit_ms = limit_ms > AUDIO_OUTPUT_MAX_QUEUED_MS ? limit_ms : AUDIO_OUTPUT_MAX_QUEUED_MS;
    if (static_cast<double>(frames_to_ms(queued, output->device_rate)) > limit_ms)
    {
        output->dropped_frames += frames;
        return;
    }

    // int16 -> float. Grows only for an unusually big block.
    size_t values = frames * CHANNELS;
    if (output->input.size() < values)
    {
        output->input.resize(values);
    }
    for (size_t i = 0; i < values; i++)
    {
        output->input[i] = static_cast<float>(samples[i]) / 32768.0f;
    }
    apply_dsp(output, output->input.data(), frames);

    // Resample to the device rate at the current speed. Room for the output
    // with some slack (the ratio is ~1, but a core at 32 kHz into 96 kHz is 3x).
    double ratio = resample_ratio(output);
    size_t output_frames = static_cast<size_t>(static_cast<double>(frames) * ratio) + 64;
    if (output->output.size() < output_frames * CHANNELS)
    {
        output->output.resize(output_frames * CHANNELS);
    }
    SRC_DATA data = {};
    data.data_in = output->input.data();
    data.input_frames = static_cast<long>(frames);
    data.src_ratio = ratio;
    // libsamplerate may stop early (output full); go on until the input is used up.
    while (data.input_frames > 0)
    {
        data.data_out = output->output.data();
        data.output_frames = static_cast<long>(output->output.size() / CHANNELS);
        int error = src_process(output->resampler, &data);
        if (error != 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio: resampler error: %s", src_strerror(error));
            return;
        }
        if (data.output_frames_gen > 0)
        {
            SDL_PutAudioStreamData(output->stream, output->output.data(),
                                   static_cast<int>(data.output_frames_gen) * BYTES_PER_FRAME);
        }
        if (data.input_frames_used == 0 && data.output_frames_gen == 0)
        {
            break;   // nothing moved: don't spin
        }
        data.data_in += data.input_frames_used * CHANNELS;
        data.input_frames -= data.input_frames_used;
    }
}

void audio_output_clear(AudioOutput* output)
{
    if (output->stream == nullptr)
    {
        return;
    }
    SDL_ClearAudioStream(output->stream);
    src_reset(output->resampler);
    output->last_push_ns = 0;   // a pause isn't a frame interval
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
    return frames_to_ms(queued_frames(output), output->device_rate);
}
