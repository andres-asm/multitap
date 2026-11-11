#pragma once

// The audio a core produced during one retro_run, kept by the host.
//
// Like video, the pointer a core passes to audio_sample_batch is only valid
// during the callback, so the host copies the samples. The buffer is cleared
// at the start of every libretro_host_run_frame(); afterwards the frontend
// reads it and plays it however it likes. Hosts never talk to an audio device.
//
// Format: interleaved stereo int16 (left, right, left, right...), at the core's
// sample rate (av_info.timing.sample_rate). One "frame" = one left+right pair.

#include <cstddef>
#include <cstdint>

struct LibretroAudioBuffer
{
    int16_t* samples         = nullptr;   // frames * 2 values, interleaved L/R
    size_t   frames          = 0;         // stereo frames stored since the last clear
    size_t   capacity_frames = 0;         // allocated size of `samples`, in stereo frames
};

// Appends `frame_count` stereo frames (frame_count * 2 int16 values), growing
// the buffer if needed. Returns false only if the allocation failed; nothing
// is appended then.
bool libretro_audio_append(LibretroAudioBuffer* buffer, const int16_t* samples, size_t frame_count);

// Forgets the stored frames but keeps the memory (no allocation per frame).
void libretro_audio_clear(LibretroAudioBuffer* buffer);

void libretro_audio_free(LibretroAudioBuffer* buffer);
