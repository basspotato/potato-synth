#pragma once
#include <array>
#include <atomic>
#include <cmath>

namespace ps
{
// The last moments of the output, for the editor's waveform and level meter. The audio thread writes and the
// GUI reads; every value is a relaxed atomic, so a read that races a write is at worst one stale sample.
class OutputScope
{
public:
    static constexpr int size = 4096;   // a power of two

    void push (const float* left, const float* right, int numSamples) noexcept
    {
        int w = writePos.load (std::memory_order_relaxed);
        float pl = 0.0f, pr = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            samples[(size_t) w].store (0.5f * (left[i] + right[i]), std::memory_order_relaxed);
            w = (w + 1) & (size - 1);
            pl = std::max (pl, std::abs (left[i]));
            pr = std::max (pr, std::abs (right[i]));
        }
        writePos.store (w, std::memory_order_release);
        if (pl > peaks[0].load (std::memory_order_relaxed))
            peaks[0].store (pl, std::memory_order_relaxed);
        if (pr > peaks[1].load (std::memory_order_relaxed))
            peaks[1].store (pr, std::memory_order_relaxed);
    }

    // The newest `count` samples (mono, (L + R) / 2), oldest first
    void snapshot (float* out, int count) const noexcept
    {
        const int end = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
            out[i] = samples[(size_t) ((end - count + i) & (size - 1))].load (std::memory_order_relaxed);
    }

    // The highest absolute sample on a channel since the last call
    float takePeak (int channel) noexcept { return peaks[(size_t) channel].exchange (0.0f, std::memory_order_relaxed); }

private:
    std::array<std::atomic<float>, size> samples {};
    std::atomic<int> writePos { 0 };
    std::array<std::atomic<float>, 2> peaks {};
};
}
