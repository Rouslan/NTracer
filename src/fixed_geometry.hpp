#ifndef fixed_geometry_hpp
#define fixed_geometry_hpp

#include <type_traits>

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
            new(begin() + i) T(f(i));

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

    template<int N,typename RealItems,typename T> struct item_array {
        static constexpr int _real_size = simd::padded_size<T>(RealItems::get(N));
        static constexpr int max_items = _real_size;

        explicit item_array(int d,v_array_allocator* =nullptr) {
            assert(d == N);
            item_array_init(items.raw,RealItems::get(N),_real_size);
        }

        item_array(const item_array &b) {
            (*this) = b;
        }

        item_array(const item_array &b,v_array_allocator*) {
            (*this) = b;
        }

        item_array(const item_array &b,shallow_copy_t) {
            (*this) = b;
        }

        item_array &operator=(const item_array &b) {
            for(int i=0; i<_real_size; ++i) items.raw[i] = b.items.raw[i];
            return *this;
        }

        int dimension() const { return N; }

        simd::packed_union_array<T,_real_size> items;

        template<size_t Size> FORCE_INLINE void store_vec(size_t n,simd::v_type<T,Size> val) {
            simd::at<Size>(items,n) = val;
        }

        template<size_t Size> FORCE_INLINE auto vec(size_t n) const {
            return simd::at<Size>(items,n);
        }

        T *data() { return items.raw; }
        const T *data() const { return items.raw; }
    };

    template<int N,typename T> struct item_store {
        typedef T item_t;

        template<typename U=T> static int v_dimension(int d) {
            return simd::padded_size<U>(d);
        }

        static const int required_d = N;

        template<typename U> using init_array = fixed::init_array<U,N>;
        template<typename U> using smaller_init_array = fixed::init_array<U,N-1>;

        template<typename RealItems,typename U=T> using type = item_array<N,RealItems,U>;

        static constexpr v_array_allocator *def_allocator = nullptr;

        static geom_allocator *new_allocator(int d,size_t items_per_block) {
            return nullptr;
        }

        template<typename> static v_array_allocator *allocator_for(geom_allocator*) {
            return nullptr;
        }
    };
}

template<int N,typename T> struct smaller_store<fixed::item_store<N,T> > {
    static_assert(N > 1,"it can't get any smaller");

    typedef fixed::item_store<N-1,T> type;
};

#endif
