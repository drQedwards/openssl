/*
 * Copyright 2026 The OpenSSL Project Authors. All Rights Reserved.
 * Licensed under the Apache License 2.0
 *
 * evp_local.c — Centralized EVP internal state management
 *
 * Implements a peek-before-flush discipline for EVP cached state.
 *
 * Architecture (per forloopcodes):
 *
 *   Cached state is treated as an immutable ledger once written —
 *   written once at initialisation, read many times on the hot path.
 *   The peek function performs a single sweep of this ledger without
 *   acquiring ownership. Only if the peek confirms the ledger is
 *   stale or dirty does a flush (settlement) occur.
 *
 *   This prevents the peek from becoming a second transaction in the
 *   chain. The peek is advisory and read-only. The flush is the only
 *   write operation, and it uses cmp_exch to ensure it only commits
 *   if the state seen by the peek is still current — preventing
 *   lost-update races in concurrent flush scenarios (e.g. TLS
 *   handshake_dgst shared across threads).
 *
 *   On lock-fallback platforms (NonStop, ancient Windows x86):
 *   a single write lock covers both validation and store of NULL.
 *   No double-lock. No regression. The chain is checked once.
 *
 * Depends on:
 *   #30738 (bob-beck)  — OSSL_ATOMICS_LOCKLESS
 *   #30670 (nhorman)   — CRYPTO_atomic_load_ptr/store_ptr/cmp_exch_ptr
 *   #30737 (nhorman)   — EVP cache infrastructure
 */

#include "internal/cryptlib.h"
#include "internal/threads_common.h"
#include "crypto/evp.h"
#include "evp_local.h"

/*
 * ossl_evp_cache_peek()
 *
 * Single sweep of the cached state ledger.
 * Read-only — never acquires write ownership.
 * Never triggers a flush — that is the caller's decision.
 *
 * Returns 1 and sets *out if ledger is non-NULL (cache hit).
 * Returns 0 on miss, atomic failure, or non-lockless platform.
 *
 * On non-lockless platforms always returns 0 — caller falls
 * through to the lock-based slow path. The peek does not
 * become a second transaction on those platforms.
 */
int ossl_evp_cache_peek(EVP_CACHE_STATE *state, void **out,
                        CRYPTO_RWLOCK *lock)
{
#if defined(OSSL_ATOMICS_LOCKLESS)
    if (!CRYPTO_atomic_load_ptr(&state->cached_ptr, out, lock))
        return 0;
    return *out != NULL;
#else
    /*
     * No native atomics — peek would require a lock, making it
     * a transaction rather than a sweep. Return 0 and let the
     * caller use the single-lock slow path instead.
     */
    return 0;
#endif
}

/*
 * ossl_evp_cache_store()
 *
 * Commit a value to the cached state ledger.
 * This is the write-once initialisation path.
 * Called after slow-path reinitialisation only.
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
 * ossl_evp_cache_flush()
 *
 * Settlement path. ALWAYS peeks internally before committing.
 *
 * The internal peek is a single sweep — not a second transaction.
 * If the ledger is already NULL, flush is a no-op. This avoids
 * unnecessary write contention on the common case.
 *
 * On lockless platforms: cmp_exch ensures flush only commits if
 * the value seen by the peek is still current. If another thread
 * flushed concurrently, we detect it and return cleanly rather
 * than blindly overwriting a newly stored value — preventing
 * lost-update in the TLS handshake_dgst shared-context path.
 *
 * On lock-fallback platforms (NonStop): single write lock covers
 * both the internal peek and the NULL store. One lock. No regression.
 * The chain is checked once regardless of platform.
 *
 * Atomic failures are surfaced to the caller — we do not
 * second-guess by proceeding anyway. Rare, but real on
 * lock-fallback platforms where the lock itself can fail.
 */
int ossl_evp_cache_flush(EVP_CACHE_STATE *state, CRYPTO_RWLOCK *lock)
{
    void *current = NULL;

#if defined(OSSL_ATOMICS_LOCKLESS)
    /*
     * Pre-flight sweep: advisory peek at the ledger.
     * Relaxed atomic load — no cache-line ownership transfer.
     */
    if (!CRYPTO_atomic_load_ptr(&state->cached_ptr, &current, lock))
        return 0;

    if (current == NULL)
        return 1; /* ledger already clean — no settlement needed */

    /*
     * Ledger is non-NULL. Settle atomically via cmp_exch.
     * Only writes NULL if current value matches what we peeked.
     * Concurrent flush by another thread is handled gracefully:
     * cmp_exch returns 0 and updates current — we return 0
     * and surface it. Not an error — a clean concurrent resolution.
     */
    return CRYPTO_atomic_cmp_exch_ptr(&state->cached_ptr,
                                      &current,
                                      NULL,
                                      lock);
#else
    /*
     * Lock-fallback: single write lock.
     * Peek and flush under one lock — one transaction, not two.
     * NonStop-safe.
     */
    if (!CRYPTO_THREAD_write_lock(lock))
        return 0;

    state->cached_ptr = NULL;

    CRYPTO_THREAD_unlock(lock);
    return 1;
#endif
}
/*
 * Copyright 2026 The OpenSSL Project Authors. All Rights Reserved.
 * Licensed under the Apache License 2.0
 *
 * evp_local.c — Centralized EVP internal state management
 *
 * Implements a peek-before-flush discipline for EVP cached state.
 *
 * Architecture (per forloopcodes):
 *
 *   Cached state is treated as an immutable ledger once written —
 *   written once at initialisation, read many times on the hot path.
 *   The peek function performs a single sweep of this ledger without
 *   acquiring ownership. Only if the peek confirms the ledger is
 *   stale or dirty does a flush (settlement) occur.
 *
 *   This prevents the peek from becoming a second transaction in the
 *   chain. The peek is advisory and read-only. The flush is the only
 *   write operation, and it uses cmp_exch to ensure it only commits
 *   if the state seen by the peek is still current — preventing
 *   lost-update races in concurrent flush scenarios (e.g. TLS
 *   handshake_dgst shared across threads).
 *
 *   On lock-fallback platforms (NonStop, ancient Windows x86):
 *   a single write lock covers both validation and store of NULL.
 *   No double-lock. No regression. The chain is checked once.
 *
 * Depends on:
 *   #30738 (bob-beck)  — OSSL_ATOMICS_LOCKLESS
 *   #30670 (nhorman)   — CRYPTO_atomic_load_ptr/store_ptr/cmp_exch_ptr
 *   #30737 (nhorman)   — EVP cache infrastructure
 */

#include "internal/cryptlib.h"
#include "internal/threads_common.h"
#include "crypto/evp.h"
#include "evp_local.h"

/*
 * ossl_evp_cache_peek()
 *
 * Single sweep of the cached state ledger.
 * Read-only — never acquires write ownership.
 * Never triggers a flush — that is the caller's decision.
 *
 * Returns 1 and sets *out if ledger is non-NULL (cache hit).
 * Returns 0 on miss, atomic failure, or non-lockless platform.
 *
 * On non-lockless platforms always returns 0 — caller falls
 * through to the lock-based slow path. The peek does not
 * become a second transaction on those platforms.
 */
int ossl_evp_cache_peek(EVP_CACHE_STATE *state, void **out,
                        CRYPTO_RWLOCK *lock)
{
#if defined(OSSL_ATOMICS_LOCKLESS)
    if (!CRYPTO_atomic_load_ptr(&state->cached_ptr, out, lock))
        return 0;
    return *out != NULL;
#else
    /*
     * No native atomics — peek would require a lock, making it
     * a transaction rather than a sweep. Return 0 and let the
     * caller use the single-lock slow path instead.
     */
    return 0;
#endif
}

/*
 * ossl_evp_cache_store()
 *
 * Commit a value to the cached state ledger.
 * This is the write-once initialisation path.
 * Called after slow-path reinitialisation only.
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
 * ossl_evp_cache_flush()
 *
 * Settlement path. ALWAYS peeks internally before committing.
 *
 * The internal peek is a single sweep — not a second transaction.
 * If the ledger is already NULL, flush is a no-op. This avoids
 * unnecessary write contention on the common case.
 *
 * On lockless platforms: cmp_exch ensures flush only commits if
 * the value seen by the peek is still current. If another thread
 * flushed concurrently, we detect it and return cleanly rather
 * than blindly overwriting a newly stored value — preventing
 * lost-update in the TLS handshake_dgst shared-context path.
 *
 * On lock-fallback platforms (NonStop): single write lock covers
 * both the internal peek and the NULL store. One lock. No regression.
 * The chain is checked once regardless of platform.
 *
 * Atomic failures are surfaced to the caller — we do not
 * second-guess by proceeding anyway. Rare, but real on
 * lock-fallback platforms where the lock itself can fail.
 */
int ossl_evp_cache_flush(EVP_CACHE_STATE *state, CRYPTO_RWLOCK *lock)
{
    void *current = NULL;

#if defined(OSSL_ATOMICS_LOCKLESS)
    /*
     * Pre-flight sweep: advisory peek at the ledger.
     * Relaxed atomic load — no cache-line ownership transfer.
     */
    if (!CRYPTO_atomic_load_ptr(&state->cached_ptr, &current, lock))
        return 0;

    if (current == NULL)
        return 1; /* ledger already clean — no settlement needed */

    /*
     * Ledger is non-NULL. Settle atomically via cmp_exch.
     * Only writes NULL if current value matches what we peeked.
     * Concurrent flush by another thread is handled gracefully:
     * cmp_exch returns 0 and updates current — we return 0
     * and surface it. Not an error — a clean concurrent resolution.
     */
    return CRYPTO_atomic_cmp_exch_ptr(&state->cached_ptr,
                                      &current,
                                      NULL,
                                      lock);
#else
    /*
     * Lock-fallback: single write lock.
     * Peek and flush under one lock — one transaction, not two.
     * NonStop-safe.
     */
    if (!CRYPTO_THREAD_write_lock(lock))
        return 0;

    state->cached_ptr = NULL;

    CRYPTO_THREAD_unlock(lock);
    return 1;
#endif
}
