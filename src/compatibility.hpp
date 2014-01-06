// Iron out compiler and standard library differences/deficiencies
#ifndef compatibility_hpp
#define compatibility_hpp

#include <cstddef>
#include <type_traits>


#ifdef __GNUC__
  #if __GNUC__ == 4
    #if __GNUC_MINOR__ < 9
namespace std {
    typedef ::max_align_t max_align_t;
}
    #endif
    #if __GNUC_MINOR__ < 8
      #define alignas(X) __attribute__ ((aligned (X)))

namespace std {
    template<typename T> struct is_trivially_destructible : has_trivial_destructor<T> {};
}
    #endif
  #endif

  #define LIKELY(X) __builtin_expect(static_cast<bool>(X),1)
  #define UNLIKELY(X) __builtin_expect(static_cast<bool>(X),0)

  #define RESTRICT __restrict__

  #ifdef __GNUC_GNU_INLINE__
    #define FORCE_INLINE inline __attribute__((gnu_inline,always_inline))
  #else
    #define FORCE_INLINE inline __attribute__((always_inline))
  #endif
#else
  #define LIKELY(X) X
  #define UNLIKELY(X) X

  #define RESTRICT

  #if defined(_MSC_VER)
    #define FORCE_INLINE __forceinline
  #else
    #define FORCE_INLINE inline
  #endif
#endif


#if defined(_WIN32) || defined(__CYGWIN__) || defined(__BEOS__)
  #define SHARED(RET) __declspec(dllexport) RET
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define SHARED(RET) RET __attribute__((visibility("default")))
#else
  #define SHARED(RET) RET
#endif

#endif
