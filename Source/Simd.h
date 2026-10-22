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

    // Integer lanes, for table lookups
    struct I
    {
        __m256i v;
        I (__m256i x) : v (x) {}
    };
    inline I truncate (F x)                       { return _mm256_cvttps_epi32 (x.v); }
    inline F toFloat (I x)                        { return _mm256_cvtepi32_ps (x.v); }
    // For each lane, the 4 consecutive floats at base + index - 1 (x[i-1], x[i], x[i+1], x[i+2]), transposed into
    // 4 vectors: 8 unaligned 4-float loads and a transpose, cheaper than 4 gathers
    inline void load4 (const float* base, I index, F& xm, F& x0, F& x1, F& x2)
    {
        alignas (32) int i[8];
        _mm256_store_si256 ((__m256i*) i, index.v);
        auto row = [&] (int lo, int hi) { return _mm256_insertf128_ps (_mm256_castps128_ps256 (_mm_loadu_ps (base + i[lo] - 1)), _mm_loadu_ps (base + i[hi] - 1), 1); };
        const __m256 m0 = row (0, 4), m1 = row (1, 5), m2 = row (2, 6), m3 = row (3, 7);
        const __m256 t0 = _mm256_unpacklo_ps (m0, m1), t1 = _mm256_unpacklo_ps (m2, m3);
        const __m256 t2 = _mm256_unpackhi_ps (m0, m1), t3 = _mm256_unpackhi_ps (m2, m3);
        xm = _mm256_shuffle_ps (t0, t1, 0x44);
        x0 = _mm256_shuffle_ps (t0, t1, 0xEE);
        x1 = _mm256_shuffle_ps (t2, t3, 0x44);
        x2 = _mm256_shuffle_ps (t2, t3, 0xEE);
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

    struct I
    {
        int v;
        I (int x) : v (x) {}
    };
    inline I truncate (F x)                       { return (int) x.v; }
    inline F toFloat (I x)                        { return (float) x.v; }
    inline void load4 (const float* base, I index, F& xm, F& x0, F& x1, F& x2)
    {
        xm = base[index.v - 1]; x0 = base[index.v]; x1 = base[index.v + 1]; x2 = base[index.v + 2];
    }
#endif
    // Scalar versions, so the same wave code serves single oscillators
    inline float select (bool mask, float a, float b) { return mask ? a : b; }
    inline float min (float a, float b)               { return std::min (a, b); }
    inline float abs (float a)                        { return std::abs (a); }
    inline float reciprocal (float x)                 { return 1.0f / x; }
}

}
