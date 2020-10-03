// Iron out compiler and standard library differences/deficiencies
#ifndef compatibility_hpp
#define compatibility_hpp


#ifdef __GNUC__
  #define LIKELY(X) __builtin_expect(static_cast<bool>(X),1)
  #define UNLIKELY(X) __builtin_expect(static_cast<bool>(X),0)

  #if __has_builtin(__builtin_assume)
    #define ASSUME(X) __builtin_assume(X)
  #else
    #define ASSUME(X) if(__builtin_expect(!(X),0)) __builtin_unreachable()
  #endif

  #define RESTRICT __restrict__

  #if defined(__GNUC_GNU_INLINE__) && !defined(__clang__)
    #define FORCE_INLINE inline __attribute__((gnu_inline,always_inline))
  #else
    #define FORCE_INLINE inline __attribute__((always_inline))
  #endif
#else
  #define LIKELY(X) X
  #define UNLIKELY(X) X

  #define ASSUME(X) (void)0

  #if defined(_MSC_VER)
    #define RESTRICT __restrict
    #define FORCE_INLINE __forceinline
  #else
    #define RESTRICT
    #define FORCE_INLINE inline
  #endif
#endif


#if defined(_WIN32) || defined(__CYGWIN__) || defined(__BEOS__)
  #define SHARED(RET) __declspec(dllexport) RET
#elif defined(__GNUC__)
  #define SHARED(RET) RET __attribute__((visibility("default")))
#else
  #define SHARED(RET) RET
#endif

#endif

