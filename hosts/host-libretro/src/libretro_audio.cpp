#include "libretro_audio.h"

#include <cstring>
#include <new>

// First allocation: enough for one frame of 48 kHz audio at 50 fps (960 frames)
// with room to spare, so most cores never trigger a second allocation.
static constexpr size_t INITIAL_CAPACITY_FRAMES = 4096;

bool libretro_audio_append(LibretroAudioBuffer* buffer, const int16_t* samples, size_t frame_count)
{
    if (frame_count == 0)
    {
        return true;
    }

    size_t needed = buffer->frames + frame_count;
    if (needed > buffer->capacity_frames)
    {
        // Double, so cores that send one sample at a time (audio_sample) don't
        // reallocate on every call. This only happens in the first frames.
        size_t new_capacity = buffer->capacity_frames > 0 ? buffer->capacity_frames * 2 : INITIAL_CAPACITY_FRAMES;
        while (new_capacity < needed)
        {
            new_capacity *= 2;
        }

        int16_t* bigger = new (std::nothrow) int16_t[new_capacity * 2];
        if (bigger == nullptr)
        {
            return false;
        }
        if (buffer->frames > 0)
        {
            std::memcpy(bigger, buffer->samples, buffer->frames * 2 * sizeof(int16_t));
        }
        delete[] buffer->samples;
        buffer->samples = bigger;
        buffer->capacity_frames = new_capacity;
    }

    std::memcpy(buffer->samples + buffer->frames * 2, samples, frame_count * 2 * sizeof(int16_t));
    buffer->frames += frame_count;
    return true;
}

void libretro_audio_clear(LibretroAudioBuffer* buffer)
{
    buffer->frames = 0;
}

void libretro_audio_free(LibretroAudioBuffer* buffer)
{
    delete[] buffer->samples;
    *buffer = LibretroAudioBuffer{};
}
