#ifndef fixed_geometry_hpp
#define fixed_geometry_hpp


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

    // set pad items to 1 to avoid dividing 0/infinity/NaN
    template<typename T> inline void item_array_init(T *items,int start,int end) {
        if constexpr(std::is_arithmetic_v<T>) {
            for(int i=start; i<end; ++i) items[i] = 1;
        }
    }

    /* Contains a type definition for an array of a SIMD type of width "Width",
    and array length of "N" divided by the number of elements in the SIMD type.
    If such a SIMD type is not available or the array length would be zero
    (rounded down), the type defined is an empty struct. */
    template<int N,int Width,typename T,typename = std::void_t<>> struct vtype_if_available {
        struct type {};
    };

    template<int N,int Width,typename T>
    struct vtype_if_available<N,Width,T,std::void_t<simd::v_type<T,Width/sizeof(T)>[N*sizeof(T)/Width]>> {
        typedef simd::v_type<T,Width/sizeof(T)> type[N*sizeof(T)/Width];
    };


    template<int N,typename RealItems,typename T,bool=std::is_arithmetic_v<T> || simd::is_v_type<T>> struct item_array {
        static constexpr int _real_size = simd::padded_size<T>(RealItems::get(N));
        static constexpr int max_items = _real_size;

        explicit item_array(int d) {
            assert(d == N);
            item_array_init(items.raw,RealItems::get(N),_real_size);
        }

        item_array(const item_array &b) {
            (*this) = b;
        }

        item_array &operator=(const item_array &b) {
            for(int i=0; i<_real_size; ++i) items.raw[i] = b.items.raw[i];
            return *this;
        }

        int dimension() const { return N; }

        union {
            T raw[_real_size];
            simd::scalar<T> s[_real_size];
            typename vtype_if_available<_real_size,16,T>::type v16;
            typename vtype_if_available<_real_size,32,T>::type v32;
            typename vtype_if_available<_real_size,64,T>::type v64;
        } items;

        template<size_t Size,typename U> static FORCE_INLINE auto &_vec(U &self,size_t n) {
            static_assert(Size <= _real_size,"trying to get vector type larger than entire array");

            assert(n % Size == 0);
            if constexpr(Size * sizeof(T) == 64) return self.items.v64[n/Size];
            else if constexpr(Size * sizeof(T) == 32) return self.items.v32[n/Size];
            else if constexpr(Size * sizeof(T) == 16) return self.items.v16[n/Size];
            else {
                static_assert(Size == 1,"a type with this size is not defined here");
                return self.items.s[n];
            }
        }

        template<size_t Size> FORCE_INLINE auto &vec(size_t n) {
            return _vec<Size>(*this,n);
        }

        template<size_t Size> FORCE_INLINE auto vec(size_t n) const {
            return _vec<Size>(*this,n);
        }
    };

    template<int N,typename RealItems,typename T> struct item_array<N,RealItems,T,false> {
        static const int _real_size = RealItems::get(N) + simd::padded_size<T>(N) - N;
        static const int max_items = _real_size;

        explicit item_array(int d) {
            assert(d == N);
            item_array_init(items.raw,RealItems::get(N),_real_size);
        }

        int dimension() const { return N; }

        struct {
            T raw[_real_size];
        } items;
    };

    template<int N,typename T> struct item_store {
        typedef T item_t;

        static int v_dimension(int d) {
            return simd::padded_size<T>(d);
        }

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
