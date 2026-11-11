// Checks libretro_audio (the host's per-frame audio buffer): samples must come
// out exactly as the core sent them, in order, across single-sample appends,
// batch appends and buffer growth; clear must forget frames but keep memory.
//
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdint>
#include <cstdio>

#include "libretro_audio.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

// The value a test writes for stereo frame `index`, channel 0 = left, 1 = right.
// Distinct per frame and channel, and uses negative numbers too.
static int16_t pattern(size_t index, size_t channel)
{
    int32_t value = static_cast<int32_t>(index % 30000) * (channel == 0 ? 1 : -1);
    return static_cast<int16_t>(value);
}

static bool matches_pattern(const LibretroAudioBuffer* buffer, size_t first_index, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (buffer->samples[i * 2] != pattern(first_index + i, 0) ||
            buffer->samples[i * 2 + 1] != pattern(first_index + i, 1))
        {
            std::printf("  mismatch at frame %zu\n", i);
            return false;
        }
    }
    return true;
}

int main()
{
    LibretroAudioBuffer buffer = {};

    // --- Empty append is a no-op and doesn't allocate.
    check(libretro_audio_append(&buffer, nullptr, 0), "empty append succeeds");
    check(buffer.frames == 0 && buffer.samples == nullptr, "empty append stores nothing");

    // --- One frame at a time (audio_sample), past the first allocation, so growth
    // must keep what was already there.
    constexpr size_t SINGLE_FRAMES = 10000;
    for (size_t i = 0; i < SINGLE_FRAMES; i++)
    {
        int16_t frame[2] = { pattern(i, 0), pattern(i, 1) };
        if (!libretro_audio_append(&buffer, frame, 1))
        {
            check(false, "single-frame append");
            break;
        }
    }
    check(buffer.frames == SINGLE_FRAMES, "single-frame appends: frame count");
    check(buffer.capacity_frames >= SINGLE_FRAMES, "single-frame appends: capacity grew");
    check(matches_pattern(&buffer, 0, SINGLE_FRAMES), "single-frame appends: samples intact after growth");

    // --- Clear keeps the memory.
    int16_t* memory_before = buffer.samples;
    size_t capacity_before = buffer.capacity_frames;
    libretro_audio_clear(&buffer);
    check(buffer.frames == 0, "clear: no frames");
    check(buffer.samples == memory_before && buffer.capacity_frames == capacity_before, "clear: memory kept");

    // --- Batches (audio_sample_batch): 800 frames = one 60 fps frame at 48 kHz.
    constexpr size_t BATCH_FRAMES = 800;
    int16_t batch[BATCH_FRAMES * 2] = {};
    for (size_t i = 0; i < BATCH_FRAMES; i++)
    {
        batch[i * 2] = pattern(i, 0);
        batch[i * 2 + 1] = pattern(i, 1);
    }
    check(libretro_audio_append(&buffer, batch, BATCH_FRAMES), "batch append");
    for (size_t i = 0; i < BATCH_FRAMES; i++)
    {
        batch[i * 2] = pattern(BATCH_FRAMES + i, 0);
        batch[i * 2 + 1] = pattern(BATCH_FRAMES + i, 1);
    }
    check(libretro_audio_append(&buffer, batch, BATCH_FRAMES), "second batch append");
    check(buffer.frames == BATCH_FRAMES * 2, "batches: frame count");
    check(matches_pattern(&buffer, 0, BATCH_FRAMES * 2), "batches: both batches in order");
    check(buffer.samples == memory_before, "batches: no reallocation when capacity suffices");

    // --- One batch bigger than the current capacity.
    libretro_audio_clear(&buffer);
    size_t big_frames = buffer.capacity_frames * 3 + 7;
    int16_t* big = new int16_t[big_frames * 2];
    for (size_t i = 0; i < big_frames; i++)
    {
        big[i * 2] = pattern(i, 0);
        big[i * 2 + 1] = pattern(i, 1);
    }
    check(libretro_audio_append(&buffer, big, big_frames), "big batch append");
    check(buffer.frames == big_frames, "big batch: frame count");
    check(matches_pattern(&buffer, 0, big_frames), "big batch: samples intact");
    delete[] big;

    libretro_audio_free(&buffer);
    check(buffer.samples == nullptr && buffer.frames == 0 && buffer.capacity_frames == 0, "free resets");

    if (g_failures == 0)
    {
        std::printf("audio buffer: all checks passed\n");
        return 0;
    }
    std::printf("audio buffer: %d check(s) failed\n", g_failures);
    return 1;
}
