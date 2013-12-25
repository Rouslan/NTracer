#ifndef fixed_geometry_hpp
#define fixed_geometry_hpp

#include <Python.h>

#include "py_common.hpp"
#include "geometry.hpp"


namespace fixed {
    /* an array that can be initialized with a call-back */
    template<typename T,size_t Size> struct init_array {
        typename std::aligned_storage<sizeof(T) * Size,alignof(T)>::type data;
        
        T *begin() { return reinterpret_cast<T*>(&data); }
        const T *begin() const { return reinterpret_cast<const T*>(&data); }
        
        T *end() { return begin() + Size; }
        const T *end() const { return begin() + Size; }
        
        T &front() { return begin()[0]; }
        const T &front() const { return begin()[0]; }
        
        T &back() { return begin()[Size-1]; }
        const T &back() const { return begin()[Size-1]; }
        
        T &operator[](int i) { return begin()[i]; }
        const T &operator[](int i) const { return begin()[i]; }
        
        operator T*() { return begin(); }
        operator const T*() const { return begin(); }
        
        size_t size() const { return size; }
        
        template<typename F> init_array(size_t size,F f) {
            assert(size == Size);
            subinit(f,0);
        }
        
        ~init_array() {
            for(T &x : *this) x.~T();
        }
        
    private:
        template<typename F> void subinit(F f,size_t i) {
            new(&(*this)[i]) T(f(i));
            
            if(i < Size-1) {
                try {
                    subinit(f,i+1);
                } catch(...) {
                    (*this)[i].~T();
                    throw;
                }
            }
        }
    };

    template<int N,typename RealItems,typename T> struct alignas(simd::largest_fit<T>(N) * sizeof(T)) item_array {
        explicit item_array(int d) {
            assert(d == N);
        }
        
        int dimension() const { return N; }
        
        T items[RealItems::get(N)];
    };
    
    template<int N,typename T> struct item_store {
        typedef T item_t;
        
        static const int required_d = N;
        
        template<typename U> using init_array = fixed::init_array<U,N>;
        template<typename U> using smaller_init_array = fixed::init_array<U,N-1>;
        
        template<typename RealItems,typename U=T> using type = item_array<N,RealItems,U>;
    };
}

template<int N,typename T> struct smaller_store<fixed::item_store<N,T> > {
    static_assert(N > 1,"it can't get any smaller");
    
    typedef fixed::item_store<N-1,T> type;
};

#endif
