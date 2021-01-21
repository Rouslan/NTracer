// Iron out compiler and standard library differences/deficiencies
#ifndef compatibility_hpp
#define compatibility_hpp

#ifndef __has_builtin
#define __has_builtin(X) 0
#endif

#ifdef __GNUC__
  #define LIKELY(X) __builtin_expect(static_cast<bool>(X),1)
  #define UNLIKELY(X) __builtin_expect(static_cast<bool>(X),0)

  #if __has_builtin(__builtin_assume)
    #define ASSUME(X) __builtin_assume(X)
  #else
    #define ASSUME(X) if(__builtin_expect(!(X),0)) __builtin_unreachable()
  #endif

  #define RESTRICT __restrict__

  #if (defined(__SSE__) || defined(__AVX__)) && !defined(__x86_64__)
    #define FIX_STACK_ALIGN __attribute__((force_align_arg_pointer))
  #else
    #define FIX_STACK_ALIGN
  #endif

  #define FORCE_INLINE inline __attribute__((always_inline))

  #define HOT_FUNC __attribute__((hot))
  #define COLD_FUNC __attribute__((cold))
#else
  #define LIKELY(X) X
  #define UNLIKELY(X) X

  #define ASSUME(X) (void)0

  #define FIX_STACK_ALIGN

  #define HOT_FUNC
  #define COLD_FUNC

  #if defined(_MSC_VER)
    #define RESTRICT __restrict
    #define FORCE_INLINE __forceinline
  #else
    #define RESTRICT
    #define FORCE_INLINE inline
  #endif
#endif

#if defined(_MSC_VER)
/* As of MSVC 2019, the MSVC compiler does not perform empty base class
optimization on classes that use multiple-inheritance, unless this attribute is
used. */
  #define ALLOW_EBO __declspec(empty_bases)
#else
  #define ALLOW_EBO
#endif


#if defined(_WIN32) || defined(__CYGWIN__) || defined(__BEOS__)
  #define SHARED(RET) __declspec(dllexport) RET
#elif defined(__GNUC__)
  #define SHARED(RET) RET __attribute__((visibility("default")))
#else
  #define SHARED(RET) RET
#endif

#endif
