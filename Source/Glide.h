#pragma once
#include <cmath>

namespace ps
{
// Constant-time pitch glide in semitones. Linear in pitch, so a glide sounds even across the keyboard,
// and it always takes the set time whatever the interval (like the JP-8000 and most modern synths).
class PitchGlide
{
public:
    void jumpTo (float semitones) noexcept
    {
        current = target = semitones;
        remaining = 0;
    }

    void glideTo (float semitones, double seconds, double sampleRate) noexcept
    {
        const int samples = (int) std::lround (seconds * sampleRate);
        if (samples <= 0)
        {
            jumpTo (semitones);
            return;
        }
        target = semitones;
        remaining = samples;
        step = (target - current) / (float) samples;
    }

    float next() noexcept
    {
        if (remaining > 0)
            current = --remaining == 0 ? target : current + step;
        return current;
    }

    bool isGliding() const noexcept { return remaining > 0; }
    float getCurrent() const noexcept { return current; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0;
};
}
