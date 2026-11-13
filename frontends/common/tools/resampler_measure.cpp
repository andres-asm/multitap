// resampler_measure: compares audio resamplers with numbers, to choose the
// one piccolo uses (plan-milestones, milestone 2). A development tool, not
// shipped; CTest runs it in a short mode as a sanity check.
//
// Usage: resampler_measure [--quick]
//
// Candidates: SDL3's audio stream (today's), libsamplerate (fastest / medium /
// best sinc), speexdsp (quality 3 / 5 / 10), libretro-common's sinc (normal /
// highest, C path: no SIMD, so its speed is understated).
//
// For each rate conversion (32040 -> 48000 like a SNES, 44100 -> 48000,
// 48000 -> 44100), stereo, fed in 1024-frame blocks like a frontend does:
//   snr_1k     signal to noise+distortion of a 1 kHz tone at -6 dBFS (dB)
//   snr_high   the same for a tone at 40% of the lower rate (dB)
//   edge       gain at 45% of the lower rate relative to 1 kHz (dB; 0 = flat)
//   reject     downsampling: how far a tone above the output's Nyquist
//              frequency is pushed down (dB, aliasing). Upsampling: how far
//              the image of a high tone above the input's Nyquist is (dB).
//   speed      times real time for stereo (higher is faster)
// Higher dB is better everywhere except `edge`, where closer to 0 is better.
// The first 4096 output frames are skipped (filter start-up).

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "samplerate.h"
#include "speex_resampler.h"
#include "audio/audio_resampler.h"

extern "C" retro_resampler_t sinc_resampler;   // libretro-common's sinc driver

enum class Kind
{
    sdl,
    libsamplerate,
    speex,
    libretro_sinc,
};

struct Candidate
{
    const char* name;
    Kind        kind;
    int         quality;   // the library's own quality value
};

static const Candidate CANDIDATES[] = {
    {"SDL3 audio stream", Kind::sdl, 0},
    {"libsamplerate fastest", Kind::libsamplerate, SRC_SINC_FASTEST},
    {"libsamplerate medium", Kind::libsamplerate, SRC_SINC_MEDIUM_QUALITY},
    {"libsamplerate best", Kind::libsamplerate, SRC_SINC_BEST_QUALITY},
    {"speexdsp q3", Kind::speex, 3},
    {"speexdsp q5", Kind::speex, 5},
    {"speexdsp q10", Kind::speex, 10},
    {"libretro sinc normal", Kind::libretro_sinc, RESAMPLER_QUALITY_NORMAL},
    {"libretro sinc highest", Kind::libretro_sinc, RESAMPLER_QUALITY_HIGHEST},
};
constexpr size_t CANDIDATE_COUNT = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);

constexpr size_t BLOCK_FRAMES = 1024;
constexpr size_t SKIP_FRAMES = 4096;
constexpr double PI = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Running one resampler over a whole stereo signal, block by block
// ---------------------------------------------------------------------------

static bool run_sdl(int in_rate, int out_rate, const std::vector<float>* in, std::vector<float>* out)
{
    SDL_AudioSpec source = {SDL_AUDIO_F32, 2, in_rate};
    SDL_AudioSpec target = {SDL_AUDIO_F32, 2, out_rate};
    SDL_AudioStream* stream = SDL_CreateAudioStream(&source, &target);
    if (stream == nullptr)
    {
        return false;
    }
    std::vector<float> chunk(BLOCK_FRAMES * 8);
    size_t frames = in->size() / 2;
    for (size_t start = 0; start < frames; start += BLOCK_FRAMES)
    {
        size_t count = frames - start < BLOCK_FRAMES ? frames - start : BLOCK_FRAMES;
        SDL_PutAudioStreamData(stream, in->data() + start * 2, static_cast<int>(count * 2 * sizeof(float)));
        int got = SDL_GetAudioStreamData(stream, chunk.data(), static_cast<int>(chunk.size() * sizeof(float)));
        for (int i = 0; i < got / static_cast<int>(sizeof(float)); i++)
        {
            out->push_back(chunk[static_cast<size_t>(i)]);
        }
    }
    SDL_DestroyAudioStream(stream);
    return true;
}

static bool run_libsamplerate(int quality, int in_rate, int out_rate, const std::vector<float>* in,
                              std::vector<float>* out)
{
    int error = 0;
    SRC_STATE* state = src_new(quality, 2, &error);
    if (state == nullptr)
    {
        return false;
    }
    std::vector<float> chunk(BLOCK_FRAMES * 8);
    size_t frames = in->size() / 2;
    for (size_t start = 0; start < frames; start += BLOCK_FRAMES)
    {
        size_t count = frames - start < BLOCK_FRAMES ? frames - start : BLOCK_FRAMES;
        SRC_DATA data = {};
        data.data_in = in->data() + start * 2;
        data.input_frames = static_cast<long>(count);
        data.src_ratio = static_cast<double>(out_rate) / static_cast<double>(in_rate);
        // It may not take a whole block in one call: keep going until it has.
        while (data.input_frames > 0)
        {
            data.data_out = chunk.data();
            data.output_frames = static_cast<long>(chunk.size() / 2);
            if (src_process(state, &data) != 0)
            {
                src_delete(state);
                return false;
            }
            out->insert(out->end(), chunk.begin(), chunk.begin() + data.output_frames_gen * 2);
            data.data_in += data.input_frames_used * 2;
            data.input_frames -= data.input_frames_used;
        }
    }
    src_delete(state);
    return true;
}

static bool run_speex(int quality, int in_rate, int out_rate, const std::vector<float>* in, std::vector<float>* out)
{
    int error = 0;
    SpeexResamplerState* state = speex_resampler_init(2, static_cast<spx_uint32_t>(in_rate),
                                                      static_cast<spx_uint32_t>(out_rate), quality, &error);
    if (state == nullptr)
    {
        return false;
    }
    speex_resampler_skip_zeros(state);
    std::vector<float> chunk(BLOCK_FRAMES * 8);
    size_t frames = in->size() / 2;
    for (size_t start = 0; start < frames; start += BLOCK_FRAMES)
    {
        size_t count = frames - start < BLOCK_FRAMES ? frames - start : BLOCK_FRAMES;
        const float* input = in->data() + start * 2;
        spx_uint32_t remaining = static_cast<spx_uint32_t>(count);
        while (remaining > 0)
        {
            spx_uint32_t used = remaining;
            spx_uint32_t produced = static_cast<spx_uint32_t>(chunk.size() / 2);
            speex_resampler_process_interleaved_float(state, input, &used, chunk.data(), &produced);
            out->insert(out->end(), chunk.begin(), chunk.begin() + produced * 2);
            input += used * 2;
            remaining -= used;
        }
    }
    speex_resampler_destroy(state);
    return true;
}

static bool run_libretro_sinc(int quality, int in_rate, int out_rate, const std::vector<float>* in,
                              std::vector<float>* out)
{
    // bandwidth_mod = the ratio it's built for (out / in), as RetroArch passes
    // it: the filter's cutoff follows it when downsampling.
    double ratio = static_cast<double>(out_rate) / static_cast<double>(in_rate);
    void* state = sinc_resampler.init(nullptr, ratio, static_cast<enum resampler_quality>(quality), 0);
    if (state == nullptr)
    {
        return false;
    }
    std::vector<float> chunk(BLOCK_FRAMES * 8);
    size_t frames = in->size() / 2;
    for (size_t start = 0; start < frames; start += BLOCK_FRAMES)
    {
        size_t count = frames - start < BLOCK_FRAMES ? frames - start : BLOCK_FRAMES;
        struct resampler_data data = {};
        data.data_in = in->data() + start * 2;
        data.data_out = chunk.data();
        data.input_frames = count;
        data.ratio = ratio;
        sinc_resampler.process(state, &data);
        out->insert(out->end(), chunk.begin(), chunk.begin() + static_cast<ptrdiff_t>(data.output_frames * 2));
    }
    sinc_resampler.free(state);
    return true;
}

static bool run(const Candidate* candidate, int in_rate, int out_rate, const std::vector<float>* in,
                std::vector<float>* out)
{
    out->clear();
    switch (candidate->kind)
    {
    case Kind::sdl:
        return run_sdl(in_rate, out_rate, in, out);
    case Kind::libsamplerate:
        return run_libsamplerate(candidate->quality, in_rate, out_rate, in, out);
    case Kind::speex:
        return run_speex(candidate->quality, in_rate, out_rate, in, out);
    case Kind::libretro_sinc:
        return run_libretro_sinc(candidate->quality, in_rate, out_rate, in, out);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Signals and measurements
// ---------------------------------------------------------------------------

static std::vector<float> tone(double frequency, int rate, double seconds, double amplitude)
{
    size_t frames = static_cast<size_t>(seconds * rate);
    std::vector<float> samples(frames * 2);
    for (size_t i = 0; i < frames; i++)
    {
        float value = static_cast<float>(amplitude * std::sin(2.0 * PI * frequency * static_cast<double>(i) / rate));
        samples[i * 2] = value;
        samples[i * 2 + 1] = value;
    }
    return samples;
}

// Least squares fit of a*sin + b*cos + c at `frequency` to the left channel
// (after the start-up frames). Returns the fitted amplitude; *residual_rms
// gets what the fit doesn't explain (noise + distortion + other tones).
static double fit_tone(const std::vector<float>* out, double frequency, int rate, double* residual_rms)
{
    size_t frames = out->size() / 2;
    if (frames <= SKIP_FRAMES * 2)
    {
        *residual_rms = 1.0;
        return 0.0;
    }
    double s_ss = 0.0, s_cc = 0.0, s_sc = 0.0, s_s = 0.0, s_c = 0.0, s_ys = 0.0, s_yc = 0.0, s_y = 0.0;
    size_t count = 0;
    for (size_t i = SKIP_FRAMES; i < frames - SKIP_FRAMES; i++)
    {
        double phase = 2.0 * PI * frequency * static_cast<double>(i) / rate;
        double s = std::sin(phase);
        double c = std::cos(phase);
        double y = static_cast<double>((*out)[i * 2]);
        s_ss += s * s;
        s_cc += c * c;
        s_sc += s * c;
        s_s += s;
        s_c += c;
        s_ys += y * s;
        s_yc += y * c;
        s_y += y;
        count++;
    }
    // Normal equations for [a b c] (3x3), solved with Cramer's rule.
    double n = static_cast<double>(count);
    double m[3][3] = {{s_ss, s_sc, s_s}, {s_sc, s_cc, s_c}, {s_s, s_c, n}};
    double r[3] = {s_ys, s_yc, s_y};
    double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                 m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    double det_a = r[0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (r[1] * m[2][2] - m[1][2] * r[2]) +
                   m[0][2] * (r[1] * m[2][1] - m[1][1] * r[2]);
    double det_b = m[0][0] * (r[1] * m[2][2] - m[1][2] * r[2]) - r[0] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                   m[0][2] * (m[1][0] * r[2] - r[1] * m[2][0]);
    double det_c = m[0][0] * (m[1][1] * r[2] - r[1] * m[2][1]) - m[0][1] * (m[1][0] * r[2] - r[1] * m[2][0]) +
                   r[0] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    double a = det_a / det;
    double b = det_b / det;
    double c0 = det_c / det;

    double residual = 0.0;
    for (size_t i = SKIP_FRAMES; i < frames - SKIP_FRAMES; i++)
    {
        double phase = 2.0 * PI * frequency * static_cast<double>(i) / rate;
        double y = static_cast<double>((*out)[i * 2]);
        double e = y - (a * std::sin(phase) + b * std::cos(phase) + c0);
        residual += e * e;
    }
    *residual_rms = std::sqrt(residual / n);
    return std::sqrt(a * a + b * b);
}

static double decibels(double ratio)
{
    return 20.0 * std::log10(ratio > 1e-12 ? ratio : 1e-12);
}

struct Results
{
    bool   ok = false;
    double snr_1k = 0.0;
    double snr_high = 0.0;
    double edge = 0.0;
    double reject = 0.0;
    double speed = 0.0;
};

static double snr(const Candidate* candidate, double frequency, int in_rate, int out_rate, double seconds,
                  double* amplitude, bool* ok)
{
    std::vector<float> in = tone(frequency, in_rate, seconds, 0.5);
    std::vector<float> out;
    *ok = run(candidate, in_rate, out_rate, &in, &out) && *ok;
    double residual = 0.0;
    *amplitude = fit_tone(&out, frequency, out_rate, &residual);
    return decibels((*amplitude / std::sqrt(2.0)) / (residual > 0.0 ? residual : 1e-12));
}

static Results measure(const Candidate* candidate, int in_rate, int out_rate, double seconds)
{
    Results results = {};
    bool ok = true;
    double low_rate = in_rate < out_rate ? in_rate : out_rate;

    double amplitude_1k = 0.0;
    results.snr_1k = snr(candidate, 1000.0, in_rate, out_rate, seconds, &amplitude_1k, &ok);
    double amplitude_high = 0.0;
    results.snr_high = snr(candidate, 0.40 * low_rate, in_rate, out_rate, seconds, &amplitude_high, &ok);
    double amplitude_edge = 0.0;
    snr(candidate, 0.45 * low_rate, in_rate, out_rate, seconds, &amplitude_edge, &ok);
    results.edge = decibels(amplitude_edge / (amplitude_1k > 0.0 ? amplitude_1k : 1e-12));

    std::vector<float> out;
    if (out_rate < in_rate)
    {
        // A tone between the two Nyquist frequencies must disappear.
        double frequency = 0.5 * (out_rate * 0.5 + in_rate * 0.5);
        std::vector<float> in = tone(frequency, in_rate, seconds, 0.5);
        ok = run(candidate, in_rate, out_rate, &in, &out) && ok;
        double sum = 0.0;
        size_t frames = out.size() / 2;
        for (size_t i = SKIP_FRAMES; i + SKIP_FRAMES < frames; i++)
        {
            sum += static_cast<double>(out[i * 2]) * static_cast<double>(out[i * 2]);
        }
        double rms = frames > SKIP_FRAMES * 2 ? std::sqrt(sum / static_cast<double>(frames - SKIP_FRAMES * 2)) : 1.0;
        results.reject = -decibels(rms / (0.5 / std::sqrt(2.0)));
    }
    else
    {
        // A high tone's image (in_rate - f) must not appear in the output.
        double frequency = 0.40 * in_rate;
        std::vector<float> in = tone(frequency, in_rate, seconds, 0.5);
        ok = run(candidate, in_rate, out_rate, &in, &out) && ok;
        double residual = 0.0;
        double image = fit_tone(&out, in_rate - frequency, out_rate, &residual);
        results.reject = -decibels(image / 0.5);
    }

    // Speed: 10 s of a 1 kHz tone (2 s in quick mode).
    double speed_seconds = seconds * 5.0;
    std::vector<float> in = tone(1000.0, in_rate, speed_seconds, 0.5);
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    ok = run(candidate, in_rate, out_rate, &in, &out) && ok;
    std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - begin;
    results.speed = elapsed.count() > 0.0 ? speed_seconds / elapsed.count() : 0.0;
    results.ok = ok;
    return results;
}

int main(int argc, char** argv)
{
    bool quick = argc > 1 && std::strcmp(argv[1], "--quick") == 0;
    double seconds = quick ? 0.4 : 2.0;

    struct Conversion
    {
        int in_rate;
        int out_rate;
    };
    static const Conversion CONVERSIONS[] = {{32040, 48000}, {44100, 48000}, {48000, 44100}};

    bool all_ok = true;
    for (size_t c = 0; c < sizeof(CONVERSIONS) / sizeof(CONVERSIONS[0]); c++)
    {
        int in_rate = CONVERSIONS[c].in_rate;
        int out_rate = CONVERSIONS[c].out_rate;
        std::printf("\n%d -> %d Hz%s\n", in_rate, out_rate, out_rate < in_rate ? " (reject = aliasing)" : " (reject = image)");
        std::printf("  %-24s %8s %9s %8s %8s %9s\n", "candidate", "snr_1k", "snr_high", "edge", "reject", "speed");
        for (size_t i = 0; i < CANDIDATE_COUNT; i++)
        {
            Results results = measure(&CANDIDATES[i], in_rate, out_rate, seconds);
            if (!results.ok)
            {
                std::printf("  %-24s FAILED\n", CANDIDATES[i].name);
                all_ok = false;
                continue;
            }
            std::printf("  %-24s %8.1f %9.1f %8.2f %8.1f %8.0fx\n", CANDIDATES[i].name, results.snr_1k,
                        results.snr_high, results.edge, results.reject, results.speed);
        }
    }
    std::printf("\nresampler measure: %s\n", all_ok ? "all candidates ran" : "some candidates FAILED");
    return all_ok ? 0 : 1;
}
