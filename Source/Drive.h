#pragma once
#include <cmath>

namespace ps
{
// tanh saturation with first-order antiderivative anti-aliasing (ADAA, Parker et al. 2016): the output is
// the average of tanh over the segment between consecutive inputs, which suppresses most of the aliasing a
// plain tanh makes at the base rate. Adds half a sample of delay. One instance per channel.
class AdaaTanh
{
public:
    void reset() noexcept { x1 = 0.0; f1 = 0.0; }

    float process (float input) noexcept
    {
        const double x = input;
        const double f = logCosh (x);
        const double dx = x - x1;
        const double y = std::abs (dx) > 1.0e-5 ? (f - f1) / dx : std::tanh (0.5 * (x + x1));
        x1 = x;
        f1 = f;
        return (float) y;
    }

private:
    // log(cosh x) without overflow
    static double logCosh (double x) noexcept
    {
        const double a = std::abs (x);
        return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531;
    }

    double x1 = 0.0, f1 = 0.0;
};
}
