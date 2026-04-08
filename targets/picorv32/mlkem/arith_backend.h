#ifndef PQC_POLY_MLKEM_ARITH_BACKEND_H
#define PQC_POLY_MLKEM_ARITH_BACKEND_H

#define MLK_USE_NATIVE_NTT
#define MLK_USE_NATIVE_INTT
#define MLK_USE_NATIVE_POLY_TOMONT
#define MLK_USE_NATIVE_POLY_MULCACHE_COMPUTE
#define MLK_USE_NATIVE_POLYVEC_BASEMUL_ACC_MONTGOMERY_CACHED
#define MLK_NATIVE_FUNC_SUCCESS 0

#if !defined(__ASSEMBLER__)

void pqc_mlkem_ntt(int16_t r[256]);
void pqc_mlkem_intt(int16_t r[256]);
void pqc_mlkem_tomont(int16_t r[256]);
void pqc_mlkem_mulcache_one(int16_t cache[128], const int16_t b[256]);
void pqc_mlkem_basemul(int16_t r[256], const int16_t *a, const int16_t *b,
                       const int16_t *cache);

static MLK_INLINE int mlk_ntt_native(int16_t r[256])
{
    pqc_mlkem_ntt(r);
    return 0;
}

#if !defined(MLK_CONFIG_NO_ENCAPS_API) || !defined(MLK_CONFIG_NO_DECAPS_API)
static MLK_INLINE int mlk_intt_native(int16_t r[256])
{
    pqc_mlkem_intt(r);
    return 0;
}
#endif

#if !defined(MLK_CONFIG_NO_KEYPAIR_API)
static MLK_INLINE int mlk_poly_tomont_native(int16_t r[256])
{
    pqc_mlkem_tomont(r);
    return 0;
}
#endif

static MLK_INLINE int mlk_poly_mulcache_compute_native(int16_t cache[128],
                                                       const int16_t b[256])
{
    pqc_mlkem_mulcache_one(cache, b);
    return 0;
}

#if defined(MLK_CONFIG_MULTILEVEL_WITH_SHARED) || MLKEM_K == 2
static MLK_INLINE int mlk_polyvec_basemul_acc_montgomery_cached_k2_native(
    int16_t r[256], const int16_t a[512], const int16_t b[512], const int16_t cache[256])
{
    pqc_mlkem_basemul(r, a, b, cache);
    return 0;
}
#endif

#if defined(MLK_CONFIG_MULTILEVEL_WITH_SHARED) || MLKEM_K == 3
static MLK_INLINE int mlk_polyvec_basemul_acc_montgomery_cached_k3_native(
    int16_t r[256], const int16_t a[768], const int16_t b[768], const int16_t cache[384])
{
    pqc_mlkem_basemul(r, a, b, cache);
    return 0;
}
#endif

#if defined(MLK_CONFIG_MULTILEVEL_WITH_SHARED) || MLKEM_K == 4
static MLK_INLINE int mlk_polyvec_basemul_acc_montgomery_cached_k4_native(
    int16_t r[256], const int16_t a[1024], const int16_t b[1024], const int16_t cache[512])
{
    pqc_mlkem_basemul(r, a, b, cache);
    return 0;
}
#endif

#endif
#endif
