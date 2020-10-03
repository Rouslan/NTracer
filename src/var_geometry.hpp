#ifndef var_geometry_hpp
#define var_geometry_hpp

#include <algorithm>

#include "geometry.hpp"


namespace var {
    /* an array that can be initialized with a call-back */
    template<typename T> struct init_array {
        size_t _size;
        void *data;

        T *begin() { return reinterpret_cast<T*>(data); }
        const T *begin() const { return reinterpret_cast<const T*>(data); }

        T *end() { return begin() + _size; }
        const T *end() const { return begin() + _size; }

        T &front() { return begin()[0]; }
        const T &front() const { return begin()[0]; }

        T &back() { return begin()[_size-1]; }
        const T &back() const { return begin()[_size-1]; }

        T &operator[](int i) { return begin()[i]; }
        const T &operator[](int i) const { return begin()[i]; }

        operator T*() { return begin(); }
        operator const T*() const { return begin(); }

        size_t size() const { return _size; }

        template<typename F> init_array(size_t _size,F f) : _size(_size) {
            assert(_size);

            data = operator new(sizeof(T) * _size);

            size_t i=0;
            try {
                for(; i<_size; ++i) new(&(*this)[i]) T(f(i));
            } catch(...) {
                while(i) (*this)[--i].~T();
                throw;
            }
        }

        init_array(const init_array&) = delete;
        init_array &operator=(const init_array&) = delete;

        ~init_array() {
            for(size_t i=0; i<_size; ++i) (*this)[i].~T();
            operator delete(data);
        }
    };

    /* Contains a type definition for a pointer to a SIMD type of width
    "Width", if available, otherwise a void pointer. */
    template<int Width,typename T,typename = std::void_t<>> struct vtype_if_available {
        typedef void *type;
    };

    template<int Width,typename T>
    struct vtype_if_available<Width,T,std::void_t<simd::v_type<T,Width/sizeof(T)>>> {
        typedef simd::v_type<T,Width/sizeof(T)> *type;
    };

    template<typename T,bool=std::is_arithmetic_v<T> || simd::is_v_type<T>> struct item_array_repr {
        /* all possible types are represented in this union to allow type
        punning */
        union {
            T *raw;
            simd::scalar<T> *s;
            typename vtype_if_available<16,T>::type v16;
            typename vtype_if_available<32,T>::type v32;
            typename vtype_if_available<64,T>::type v64;
        } items;

        template<size_t Size,typename U> static FORCE_INLINE auto &_vec(U &self,size_t n) {
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

    template<typename T> struct item_array_repr<T,false> {
        struct {
            T *raw;
        } items;
    };

    template<typename RealItems,typename T> struct item_array : item_array_repr<T> {
        static const int max_items = std::numeric_limits<int>::max();

        explicit item_array(int d) : size(d) {
            allocate();
        }

        item_array(const item_array &b) : size(b.size) {
            allocate();
            memcpy(this->items.raw,b.items.raw,RealItems::get(size) * sizeof(T));
        }

        item_array(item_array &&b) : size(b.size) {
            this->items.raw = b.items.raw;
            b.items.raw = nullptr;
        }

        ~item_array() {
            deallocate();
        }

        item_array &operator=(const item_array &b) {
            deallocate();
            size = b.size;
            allocate();
            memcpy(this->items.raw,b.items.raw,RealItems::get(size) * sizeof(T));

            return *this;
        }

        item_array &operator=(item_array &&b) noexcept {
            deallocate();
            size = b.size;
            this->items.raw = b.items.raw;
            b.items.raw = nullptr;
            return *this;
        }

        int dimension() const {
            return size;
        }

        void swap(item_array &b) {
            std::swap(size,b.size);
            std::swap(this->items.raw,b.items.raw);
        }

    private:
        void allocate() {
            assert(size > 0);

            if(simd::v_sizes<T>::value[0] == 1) {
                if(!(this->items.raw = reinterpret_cast<T*>(malloc(RealItems::get(size) * sizeof(T))))) throw std::bad_alloc();
            } else {
                this->items.raw = reinterpret_cast<T*>(simd::aligned_alloc(
                    simd::largest_fit<T>(size) * sizeof(T),
                    RealItems::get(size) * sizeof(T)));
            }
        }

        void deallocate() {
            if(simd::v_sizes<T>::value[0] == 1) free(this->items.raw);
            else simd::aligned_free(this->items.raw);
        }

        int size;
    };

    template<typename T> struct item_store {
        typedef T item_t;

        static int v_dimension(int d) {
            return d;
        }

        static const int required_d = 0;

        template<typename U> using init_array = var::init_array<U>;
        template<typename U> using smaller_init_array = var::init_array<U>;

        template<typename RealItems,typename U=T> using type = item_array<RealItems,U>;
    };
}

template<typename RealItems,typename T> inline void swap(var::item_array<RealItems,T> &a,var::item_array<RealItems,T> &b) {
    a.swap(b);
}

#endif
