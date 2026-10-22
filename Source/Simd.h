#pragma once
#include <algorithm>
#include <cmath>
#if defined (__AVX2__)
 #include <immintrin.h>
#endif

namespace ps
{
// A minimal float vector: 8 lanes with AVX2 (the x64 baseline), one lane otherwise, so the same kernel code
// builds everywhere. Comparisons return lane masks for select().
namespace simd
{
#if defined (__AVX2__)
    struct F
    {
        static constexpr int width = 8;
        __m256 v;
        F() = default;
        F (__m256 x) : v (x) {}
        F (float x) : v (_mm256_set1_ps (x)) {}
        static F load (const float* p)  { return _mm256_loadu_ps (p); }
        void store (float* p) const     { _mm256_storeu_ps (p, v); }
        friend F operator+ (F a, F b)   { return _mm256_add_ps (a.v, b.v); }
        friend F operator- (F a, F b)   { return _mm256_sub_ps (a.v, b.v); }
        friend F operator* (F a, F b)   { return _mm256_mul_ps (a.v, b.v); }
        friend F operator/ (F a, F b)   { return _mm256_div_ps (a.v, b.v); }
        friend F operator< (F a, F b)   { return _mm256_cmp_ps (a.v, b.v, _CMP_LT_OQ); }
        friend F operator> (F a, F b)   { return _mm256_cmp_ps (a.v, b.v, _CMP_GT_OQ); }
        friend F operator>= (F a, F b)  { return _mm256_cmp_ps (a.v, b.v, _CMP_GE_OQ); }
    };
    inline F select (F mask, F a, F b) { return _mm256_blendv_ps (b.v, a.v, mask.v); }
    inline F min (F a, F b)            { return _mm256_min_ps (a.v, b.v); }
    inline F abs (F a)                 { return _mm256_andnot_ps (_mm256_set1_ps (-0.0f), a.v); }
    // 1/x to about 23 bits: hardware estimate plus one Newton step
    inline F reciprocal (F x)
    {
        const __m256 r = _mm256_rcp_ps (x.v);
        return _mm256_mul_ps (r, _mm256_fnmadd_ps (x.v, r, _mm256_set1_ps (2.0f)));
    }
    // out[i] += sum of v[i]'s lanes, for 8 consecutive vectors (a transpose-and-add, cheaper than 8 sums)
    inline void addSums8 (const F* v, float* out)
    {
        const __m256 a = _mm256_hadd_ps (_mm256_hadd_ps (v[0].v, v[1].v), _mm256_hadd_ps (v[2].v, v[3].v));
        const __m256 b = _mm256_hadd_ps (_mm256_hadd_ps (v[4].v, v[5].v), _mm256_hadd_ps (v[6].v, v[7].v));
        const __m256 sums = _mm256_add_ps (_mm256_permute2f128_ps (a, b, 0x20), _mm256_permute2f128_ps (a, b, 0x31));
        _mm256_storeu_ps (out, _mm256_add_ps (_mm256_loadu_ps (out), sums));
    }
    inline float sum (F a)
    {
        __m128 x = _mm_add_ps (_mm256_castps256_ps128 (a.v), _mm256_extractf128_ps (a.v, 1));
        x = _mm_add_ps (x, _mm_movehl_ps (x, x));
        x = _mm_add_ss (x, _mm_movehdup_ps (x));
        return _mm_cvtss_f32 (x);
    }
#else
    struct F
    {
        static constexpr int width = 1;
        float v;
        F() = default;
        F (float x) : v (x) {}
        static F load (const float* p)  { return *p; }
        void store (float* p) const     { *p = v; }
        friend F operator+ (F a, F b)   { return a.v + b.v; }
        friend F operator- (F a, F b)   { return a.v - b.v; }
        friend F operator* (F a, F b)   { return a.v * b.v; }
        friend F operator/ (F a, F b)   { return a.v / b.v; }
        friend F operator< (F a, F b)   { return a.v < b.v ? 1.0f : 0.0f; }
        friend F operator> (F a, F b)   { return a.v > b.v ? 1.0f : 0.0f; }
        friend F operator>= (F a, F b)  { return a.v >= b.v ? 1.0f : 0.0f; }
    };
    inline F select (F mask, F a, F b) { return mask.v > 0.5f ? a : b; }
    inline F min (F a, F b)            { return std::min (a.v, b.v); }
    inline F abs (F a)                 { return std::abs (a.v); }
    inline float sum (F a)             { return a.v; }
    inline void addSums8 (const F* v, float* out) { for (int i = 0; i < 8; ++i) out[i] += v[i].v; }
    inline F reciprocal (F x)          { return 1.0f / x.v; }
#endif
    // Scalar versions, so the same wave code serves single oscillators
    inline float select (bool mask, float a, float b) { return mask ? a : b; }
    inline float min (float a, float b)               { return std::min (a, b); }
    inline float abs (float a)                        { return std::abs (a); }
    inline float reciprocal (float x)                 { return 1.0f / x; }
}

}
