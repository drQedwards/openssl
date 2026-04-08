void CRYPTO_THREAD_clean_local(void);

/* Do atomics work? */

#if defined(__apple_build_version__) && __apple_build_version__ < 6000000
/*
 * OS/X 10.7 and 10.8 had a weird version of clang which has __ATOMIC_ACQUIRE and
 * __ATOMIC_ACQ_REL but which expects only one parameter for __atomic_is_lock_free()
 * rather than two which has signature __atomic_is_lock_free(sizeof(_Atomic(T))).
 * All of this makes impossible to use __atomic_is_lock_free here.
 *
 * See: https://github.com/llvm/llvm-project/commit/a4c2602b714e6c6edb98164550a5ae829b2de760
 */
#define BROKEN_CLANG_ATOMICS
#endif

/*
 * VC++ 2008 or earlier x86 compilers do not have an inline implementation
 * of InterlockedOr64 for 32bit and will fail to run on Windows XP 32bit.
 * https://docs.microsoft.com/en-us/cpp/intrinsics/interlockedor-intrinsic-functions#requirements
 * To work around this problem, we implement a manual locking mechanism for
 * only VC++ 2008 or earlier x86 compilers.
 */

#if defined(_MSC_VER)
#if (!defined(_M_IX86) || _MSC_VER > 1600)
#define USE_INTERLOCKEDOR64
#endif
#elif defined(__MINGW64__)
#define USE_INTERLOCKEDOR64
#endif

#if defined(__GNUC__) && defined(__ATOMIC_ACQUIRE) && !defined(BROKEN_CLANG_ATOMICS) \
    && !defined(USE_ATOMIC_FALLBACKS)
#define OSSL_USE_GCC_ATOMICS
#elif defined(__sun) && (defined(__SunOS_5_10) || defined(__SunOS_5_11))
#define OSSL_USE_SOLARIS_ATOMICS
#endif

/* Allow us to know if atomics will be implemented with a fallback lock or not. */
#if defined(OSSL_USE_GCC_ATOMICS) || defined(OSSL_USE_SOLARIS_ATOMICS) || defined(USE_INTERLOCKEDOR64)
#define OSSL_ATOMICS_LOCKLESS
#endif

#endif
