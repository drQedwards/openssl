/*
 * Copyright 2026 The OpenSSL Project Authors. All Rights Reserved.
 * Licensed under the Apache License 2.0
 *
 * evp_local.c — Centralized EVP internal state management (rebased skeleton)
 *
 * Peek-before-flush discipline for the per-thread EVP cache (#30737).
 * Depends on:
 *   #30738 (bob-beck) — OSSL_ATOMICS_LOCKLESS + atomics cleanup
 *   #30670 (nhorman)  — CRYPTO_atomic_*_ptr family
 *   #30737 (nhorman)  — per-thread cache infrastructure
 */

#include "internal/cryptlib.h"
#include "internal/threads_common.h"
#include "crypto/evp.h"
#include "evp_local.h"

/*
 * ossl_evp_cache_peek() — single advisory sweep (read-only)
 * On OSSL_ATOMICS_LOCKLESS: pure atomic load, no ownership.
 * On fallback platforms: always return 0 → caller uses single-lock slow path.
 */
int ossl_evp_cache_peek(EVP_CACHE_STATE *state, void **out,
                        CRYPTO_RWLOCK *lock)
{
#if defined(OSSL_ATOMICS_LOCKLESS)  /* ← forced special case from #30738 */
    if (!CRYPTO_atomic_load_ptr(&state->cached_ptr, out, lock))
        return 0;
    return *out != NULL;
#else
    return 0;  /* non-lockless → let caller do the single-lock path */
#endif
}

/*
 * ossl_evp_cache_store() — write-once initialization path
 */
int ossl_evp_cache_store(EVP_CACHE_STATE *state, void *val,
                         CRYPTO_RWLOCK *lock)
{
#if defined(OSSL_ATOMICS_LOCKLESS)
    return CRYPTO_atomic_store_ptr(&state->cached_ptr, &val, lock);
#else
    if (!CRYPTO_THREAD_write_lock(lock))
        return 0;
    state->cached_ptr = val;
    CRYPTO_THREAD_unlock(lock);
    return 1;
#endif
}

/*
 * ossl_evp_cache_flush() — settlement path with internal peek
 * Forced special OR case (#30738 bob-beck):
 *   LOCKLESS:   cmp_exch after relaxed peek (race-free, no lost updates)
 *   FALLBACK:   single write-lock for peek+NULL-store (NonStop-safe, no double-lock)
 */
int ossl_evp_cache_flush(EVP_CACHE_STATE *state, CRYPTO_RWLOCK *lock)
{
    void *current = NULL;
#if defined(OSSL_ATOMICS_LOCKLESS)
    if (!CRYPTO_atomic_load_ptr(&state->cached_ptr, &current, lock))
        return 0;
    if (current == NULL)
        return 1;  /* already clean */

    return CRYPTO_atomic_cmp_exch_ptr(&state->cached_ptr, &current,
                                      NULL, lock);
#else
    /* Special fallback case: ONE lock covers validation + store */
    if (!CRYPTO_THREAD_write_lock(lock))
        return 0;
    state->cached_ptr = NULL;
    CRYPTO_THREAD_unlock(lock);
    return 1;
#endif
}
